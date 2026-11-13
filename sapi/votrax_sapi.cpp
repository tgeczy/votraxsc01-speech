// license:BSD-3-Clause
// copyright-holders:tgeczy
//
// votrax_sapi.cpp -- a SAPI 5 voice for the Votrax SC-01, in one readable
// file.  The COM scaffold follows panthera-speech's engine; the Speak()
// conventions follow TGSpeechBox's, the tried and tested reference:
//
//   * JAWS sends every word as its own fragment with a bookmark between
//     them -- fragments are BATCHED and synthesized as one utterance, and
//     bookmark events fire at proportional byte offsets, so coarticulation
//     survives and word tracking still works.
//   * A fragment's rate, volume and pitch combine with the program's own
//     (the rate adds, the volume is a share of it), and a change inside a
//     request starts a new batch WITHOUT stopping the chip: its inflection
//     input and the hold of each phone change on the fly, as they do in
//     the NVDA driver.
//   * SPVES_ABORT / SPVES_SKIP are polled between audio chunks.  A skip
//     moves by sentences: a batch is rendered whole before it is written,
//     so the share of its bytes SAPI took places us in its text.
//   * site->Write() returning 0 bytes means "buffer full" on some hosts
//     (Win7-era SAPI), and it may take only a part: the rest is offered
//     again, after a short wait when nothing was taken, rather than lost.
//   * A short silence tail is appended (and offered until taken, like any
//     audio) so hosts that stop the device the moment Speak() returns do
//     not clip the final phoneme.
//
// The chip itself is the same core the NVDA add-on uses (src/core +
// src/chip), linked statically -- there is no DLL chain to break.
//
// Output format is fixed at 40000 Hz (the chip's rate at its 720 kHz
// datasheet clock).  SAPI rate, by default, changes tempo at constant
// pitch by phone truncation (holding each phone for a shorter time); the
// "authentic rate" voices instead vary the emulated master clock -- how
// real Votrax hardware sped up, pitch shift and all.  Either way the audio
// is linearly resampled back onto the fixed output rate.

#define NOMINMAX
#include <windows.h>
#include <olectl.h>
#include <sapi.h>
#include <sapiddk.h>
#include <sperror.h>

#include <algorithm>
#include <cwctype>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>

#include "sc01.h"
#include "text_to_votrax.h"
#include "timestretch.h"

// {F0C7A2B4-5C01-4D8E-A0B3-7E0193C1D2E4}  ("5C01" on purpose)
static const CLSID CLSID_VotraxSC01 =
	{ 0xf0c7a2b4, 0x5c01, 0x4d8e, { 0xa0, 0xb3, 0x7e, 0x01, 0x93, 0xc1, 0xd2, 0xe4 } };

static LONG g_objects = 0;

//==========================================================================
// small utilities
//==========================================================================

static std::wstring module_dir()
{
	extern HMODULE g_module;
	wchar_t path[MAX_PATH];
	GetModuleFileNameW(g_module, path, MAX_PATH);
	std::wstring s(path);
	size_t slash = s.find_last_of(L'\\');
	return slash == std::wstring::npos ? s : s.substr(0, slash);
}

HMODULE g_module = nullptr;

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH) {
		g_module = (HMODULE)inst;
		DisableThreadLibraryCalls(inst);
	}
	return TRUE;
}

// The two voices this DLL registers, one per mask revision.  The rate
// model (constant-pitch phone truncation by default, or "authentic"
// master-clock) is NOT a separate voice -- it is a machine/user registry
// flag the engine reads at load; see read_authentic_flag().
struct variant_info {
	const wchar_t *token;      // registry token key name
	const wchar_t *display;    // what the user sees in voice lists
	const wchar_t *rom;        // ROM file expected beside the DLL
	int variant;               // VX_SC01 / VX_SC01A
};
static const variant_info k_variants[2] = {
	{ L"VotraxSC01",  L"Votrax SC-01 (emulated)",   L"sc01.bin",  VX_SC01 },
	{ L"VotraxSC01A", L"Votrax SC-01-A (emulated)", L"sc01a.bin", VX_SC01A },
};

// "Authentic rate" is off by default (the clean constant-pitch voice).
// When set, rate becomes the chip's master clock -- faster and higher, the
// 1980 hardware's one knob.  Read once at voice load, so changing it takes
// effect the next time the host program starts (the label in the installer
// says as much).  HKCU wins over HKLM, so a user can override the
// machine-wide installer setting without admin rights.
// Test builds only (build.ps1 -Target sapitest, /DVOTRAX_SAPI_TEST): a
// fault put back by name, so tests/sapi_requests_test.py can prove that
// each of its checks fails on what it guards against.  The shipped DLL has
// no such switch.
#ifdef VOTRAX_SAPI_TEST
static bool test_break(const char *name)
{
	char value[32] = "";
	GetEnvironmentVariableA("VOTRAX_SAPI_TEST_BREAK", value, sizeof value);
	return !strcmp(value, name);
}
#else
static bool test_break(const char *) { return false; }
#endif

static bool read_authentic_flag()
{
#ifdef VOTRAX_SAPI_TEST
	// The test names the rate model itself, whatever this machine's own
	// installed voice is set to.
	char value[8] = "";
	if (GetEnvironmentVariableA("VOTRAX_SAPI_TEST_AUTHENTIC", value, sizeof value))
		return value[0] == '1';
#endif
	for (HKEY root : { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE }) {
		DWORD val = 0, sz = sizeof(val);
		if (RegGetValueW(root, L"Software\\votraxsc01", L"AuthenticRate",
				RRF_RT_REG_DWORD, nullptr, &val, &sz) == ERROR_SUCCESS)
			return val != 0;
	}
	return false;
}

static bool read_file(const std::wstring &path, std::vector<unsigned char> &out)
{
	HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
		OPEN_EXISTING, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	DWORD size = GetFileSize(h, nullptr), got = 0;
	out.resize(size);
	BOOL ok = ReadFile(h, out.data(), size, &got, nullptr);
	CloseHandle(h);
	return ok && got == size;
}

static std::string narrow(const std::wstring &w)
{
	std::string s(w.size() * 3 + 1, '\0');
	int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
		s.data(), (int)s.size(), nullptr, nullptr);
	s.resize(n > 0 ? n : 0);
	return s;
}

//==========================================================================
// the synthesis half: chip + frontend + resampler
//==========================================================================

// Fixed output rate.  The chip renders at 40000 Hz (VX_DEFAULT_CLOCK/18);
// we resample to 22050 on the way out -- the standard SAPI rate the golden
// reference engines use, well within the Votrax's <5 kHz formant range, and
// a touch lighter to move than 40000.  (Real hosts drive the voice through
// ISpVoice, which is validated; note that .NET System.Speech's
// SetOutputToWaveFile sink has a separate quirk that can stall on this
// engine -- screen readers do not use that path.)
static const DWORD k_output_rate = 22050;

class Synth
{
public:
	// Lazily creates the chip for `variant`; returns false if the ROM is
	// missing or rejected (wrong CRC), with the reason in last_error.
	bool ensure(int variant)
	{
		if (m_chip && m_variant == variant)
			return true;
		close();
		for (const variant_info &vi : k_variants) {
			if (vi.variant != variant)
				continue;
			std::vector<unsigned char> rom;
			if (!read_file(module_dir() + L"\\" + vi.rom, rom)) {
				last_error = "cannot read ROM beside the DLL";
				return false;
			}
			char err[256] = "";
			m_chip = vx_create(variant, VX_DEFAULT_CLOCK, rom.data(),
				(uint32_t)rom.size(), err, sizeof err);
			if (!m_chip) {
				last_error = err;
				return false;
			}
			m_variant = variant;
			measure_naturals();
			return true;
		}
		last_error = "unknown variant";
		return false;
	}

	// Each phone's natural length in samples at the base clock, measured
	// once per chip: write it out of silence and render until the chip
	// asks for the next.  Truncation scales these to change tempo at
	// constant pitch.
	void measure_naturals()
	{
		vx_set_clock(m_chip, VX_DEFAULT_CLOCK);
		short block[512];
		for (int ph = 0; ph < 64; ph++) {
			vx_reset(m_chip);
			vx_render(m_chip, block, 64);           // settle the reset
			vx_write(m_chip, (uint8_t)ph);
			int total = 0;
			while (!vx_ready(m_chip) && total < 400000)
				total += vx_render(m_chip, block, 512);
			m_natural[ph] = total;
		}
		vx_reset(m_chip);
	}

	void close()
	{
		if (m_chip) {
			vx_destroy(m_chip);
			m_chip = nullptr;
		}
	}

	~Synth() { close(); }

	// Render one utterance: translate, feed, resample to k_output_rate.
	// `rate_adj` is SAPI's -10..10; `inflection` 0..3; `volume` 0..100.
	// `spell` selects letter-by-letter translation.  `authentic` selects
	// the rate model: false = constant pitch by phone truncation (clean,
	// the default); true = rate as the master clock (faster and higher,
	// the 1980 hardware's one knob).
	//
	// `close` ends it the way the hardware would, with STOP and a ring-out.
	// Without it the chip is left on its last phone, for a batch that
	// follows at another rate, volume or pitch: `follows` says this is that
	// batch.  A chip left open that nothing follows (a purge or a skip came
	// between) is silenced first, or its last phone would glide into this
	// utterance's first.
	void utterance(const std::wstring &text, long rate_adj, int inflection,
	               int volume, bool spell, bool authentic, bool follows,
	               bool close, std::vector<short> &out)
	{
		out.clear();
		if (!m_chip)
			return;
		const bool was_open = m_open && follows;
		if (m_open && !follows)
			vx_reset(m_chip);
		m_open = false;

		double speed = std::pow(2.0, (double)rate_adj / 10.0);   // rate 10 = 2x
		if (authentic)
			vx_set_clock(m_chip, (uint32_t)(VX_DEFAULT_CLOCK * speed));
		else
			vx_set_clock(m_chip, VX_DEFAULT_CLOCK);
		vx_inflection(m_chip, (uint8_t)inflection);

		std::string utf8 = narrow(text);
		std::vector<uint8_t> phones(utf8.size() * 8 + 64);
		int n = (spell ? ttv_spell : ttv_translate)(
			utf8.c_str(), phones.data(), (int)phones.size());
		phones.resize(n > 0 ? n : 0);
		// Nothing to say: nothing is rendered, unless a chip left open by
		// the batch before has to be closed here.
		if (phones.empty() && !(was_open && close)) {
			m_open = was_open;
			return;
		}

		// The guard is a hard ceiling -- generous samples per phone -- so no
		// misunderstanding of the ready line can ever hang a host.
		std::vector<short> native;
		short block[512];
		long long rendered = 0;
		const long long ceiling =
			(long long)vx_sample_rate(m_chip) * ((long long)phones.size() + 4);

		if (authentic) {
			// Master clock does the timing: write when the chip asks, and
			// each phone plays its natural length (at the scaled clock).
			size_t next = 0;
			while (next < phones.size() && rendered < ceiling) {
				if (vx_ready(m_chip))
					vx_write(m_chip, phones[next++]);
				int got = vx_render(m_chip, block, 512);
				native.insert(native.end(), block, block + got);
				rendered += got;
			}
			// The last phone was only written; let it finish sounding
			// (ready re-asserts at phone end) before STOP, or the utterance
			// loses its final phone.
			while (!vx_ready(m_chip) && rendered < ceiling) {
				int got = vx_render(m_chip, block, 512);
				native.insert(native.end(), block, block + got);
				rendered += got;
			}
		} else {
			// Constant pitch: hold each phone for natural/speed samples,
			// writing the next early to shorten it.  The chip's own analog
			// output, no time-stretch, so no graininess.
			for (uint8_t ph : phones) {
				if (rendered >= ceiling)
					break;
				vx_write(m_chip, ph);
				int nat = m_natural[ph & 0x3f];
				if (nat <= 0)
					nat = k_default_hold;
				int target = (int)(nat / speed);
				// Never cut a phone before its delayed noise or voice has
				// started (S hisses only from tick 8 of 16, T bursts from
				// 7); a plain nat/speed silenced them at faster rates.
				int floor = vx_min_hold(m_chip, ph);
				if (floor > nat)
					floor = nat;
				if (target < floor)
					target = floor;
				if (target < 1)
					target = 1;
				int got = 0;
				while (got < target && rendered < ceiling) {
					int want = target - got < 512 ? target - got : 512;
					int g = vx_render(m_chip, block, want);
					native.insert(native.end(), block, block + g);
					got += g;
					rendered += g;
				}
			}
		}
		if (close) {
			vx_write(m_chip, VX_PHONE_STOP);
			for (int tail = 0; tail < (int)(vx_sample_rate(m_chip) / 4); tail += 512) {
				int got = vx_render(m_chip, block, 512);
				native.insert(native.end(), block, block + got);
			}
		} else {
			m_open = true;
		}

		resample(native, vx_sample_rate(m_chip), volume, out);
	}

	std::string last_error;

private:
	// Linear resample native-rate audio onto the fixed output rate, with
	// volume applied on the way through.  For rate 0 this is a copy.
	static void resample(const std::vector<short> &in, double in_rate,
	                     int volume, std::vector<short> &out)
	{
		if (in.empty())
			return;
		double gain = std::clamp(volume, 0, 100) / 100.0;
		double step = in_rate / (double)k_output_rate;
		size_t count = (size_t)(in.size() / step);
		out.resize(count);
		double pos = 0.0;
		for (size_t i = 0; i < count; i++, pos += step) {
			size_t i0 = (size_t)pos;
			size_t i1 = std::min(i0 + 1, in.size() - 1);
			double frac = pos - (double)i0;
			double v = (in[i0] * (1.0 - frac) + in[i1] * frac) * gain;
			out[i] = (short)std::clamp(v, -32768.0, 32767.0);
		}
	}

	vx_chip *m_chip = nullptr;
	int m_variant = -1;
	bool m_open = false;                // the last utterance was not closed
	int m_natural[64] = { 0 };          // phone -> natural length (samples)
	static const int k_default_hold = 3840;   // ~96 ms fallback
};

//==========================================================================
// the SAPI engine object
//==========================================================================

// Where a text's sentences begin, for a skip request: its first word, and
// the first word after a full stop, a question mark or an exclamation mark
// (with any closing quote or bracket) that white space follows.  "3.50" is
// no boundary.
static void sentence_starts(const std::wstring &t, std::vector<size_t> &out)
{
	size_t i = 0;
	const size_t n = t.size();
	bool start = true;
	while (i < n) {
		if (start) {
			while (i < n && iswspace(t[i]))
				i++;
			if (i >= n)
				break;
			out.push_back(i);
			start = false;
		}
		wchar_t c = t[i++];
		if (c != L'.' && c != L'?' && c != L'!')
			continue;
		size_t j = i;
		while (j < n && t[j] && wcschr(L".?!\"')]\x201D\x2019", t[j]))
			j++;
		if (j >= n || iswspace(t[j])) {
			i = j;
			start = true;
		}
	}
}

class Engine : public ISpTTSEngine, public ISpObjectWithToken
{
	LONG m_refs = 1;
	ISpObjectToken *m_token = nullptr;
	int m_variant = VX_SC01A;
	bool m_authentic = false;
	Synth m_synth;
	std::mutex m_speak_mutex;

public:
	Engine() { InterlockedIncrement(&g_objects); }
	~Engine()
	{
		if (m_token)
			m_token->Release();
		InterlockedDecrement(&g_objects);
	}

	// ---- IUnknown ---------------------------------------------------

	STDMETHODIMP QueryInterface(REFIID iid, void **out)
	{
		if (!out)
			return E_POINTER;
		*out = nullptr;
		if (iid == IID_IUnknown || iid == IID_ISpTTSEngine)
			*out = static_cast<ISpTTSEngine *>(this);
		else if (iid == IID_ISpObjectWithToken)
			*out = static_cast<ISpObjectWithToken *>(this);
		else
			return E_NOINTERFACE;
		AddRef();
		return S_OK;
	}
	STDMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_refs); }
	STDMETHODIMP_(ULONG) Release()
	{
		ULONG n = InterlockedDecrement(&m_refs);
		if (!n)
			delete this;
		return n;
	}

	// ---- ISpObjectWithToken -----------------------------------------

	STDMETHODIMP SetObjectToken(ISpObjectToken *token)
	{
		if (!token)
			return E_INVALIDARG;
		if (m_token)
			m_token->Release();
		m_token = token;
		m_token->AddRef();

		// The token's Attributes say which mask revision this voice is.
		ISpDataKey *attrs = nullptr;
		if (SUCCEEDED(token->OpenKey(L"Attributes", &attrs)) && attrs) {
			wchar_t *val = nullptr;
			if (SUCCEEDED(attrs->GetStringValue(L"VotraxVariant", &val)) && val) {
				m_variant = (wcscmp(val, L"sc01") == 0) ? VX_SC01 : VX_SC01A;
				CoTaskMemFree(val);
			}
			attrs->Release();
		}
		// Rate model is a registry flag, not a per-voice attribute.
		m_authentic = read_authentic_flag();
		return S_OK;
	}

	STDMETHODIMP GetObjectToken(ISpObjectToken **token)
	{
		if (!token)
			return E_POINTER;
		*token = m_token;
		if (m_token)
			m_token->AddRef();
		return m_token ? S_OK : S_FALSE;
	}

	// ---- ISpTTSEngine -----------------------------------------------

	STDMETHODIMP GetOutputFormat(const GUID *, const WAVEFORMATEX *,
	                             GUID *fmt_id, WAVEFORMATEX **fmt_out)
	{
		if (!fmt_id || !fmt_out)
			return E_POINTER;
		*fmt_id = SPDFID_WaveFormatEx;
		auto *fmt = (WAVEFORMATEX *)CoTaskMemAlloc(sizeof(WAVEFORMATEX));
		if (!fmt)
			return E_OUTOFMEMORY;
		fmt->wFormatTag = WAVE_FORMAT_PCM;
		fmt->nChannels = 1;
		fmt->nSamplesPerSec = k_output_rate;
		fmt->wBitsPerSample = 16;
		fmt->nBlockAlign = 2;
		fmt->nAvgBytesPerSec = k_output_rate * 2;
		fmt->cbSize = 0;
		*fmt_out = fmt;
		return S_OK;
	}

	STDMETHODIMP Speak(DWORD, REFGUID, const WAVEFORMATEX *,
	                   const SPVTEXTFRAG *frags, ISpTTSEngineSite *site)
	{
		if (!frags || !site)
			return E_INVALIDARG;
		std::lock_guard<std::mutex> lock(m_speak_mutex);
		if (!m_synth.ensure(m_variant))
			return E_FAIL;   // ROM missing/rejected; reason in last_error

		// The program's own rate and volume (ISpVoice::SetRate, SetVolume);
		// a fragment's markup combines with them.
		long site_rate = 0;
		if (FAILED(site->GetRate(&site_rate)))
			site_rate = 0;
		site_rate = std::clamp(site_rate, -10L, 10L);
		USHORT site_volume = 100;
		if (FAILED(site->GetVolume(&site_volume)) || site_volume > 100)
			site_volume = 100;

		// "markup": a control, the engine as it was up to 1.1.1 -- a
		// fragment's rate or volume REPLACED the program's (<rate speed="2">
		// under a program rate of 2 was 2, not 4; <volume level="50"> under
		// a program volume of 50 was 50, not 25), and a batch spoke all its
		// fragments with its first one's rate, volume and pitch.
		const bool old_markup = test_break("markup");

		// ---- Phase 1: batch the fragment list (the JAWS convention) --
		// Fragments are joined for as long as they ask for the same rate,
		// volume and inflection level.  Where one asks for another (a
		// <rate>, <volume> or <pitch> inside the text) a new batch begins,
		// and the chip is not stopped between the two.
		struct bookmark { std::wstring mark; size_t char_offset; };
		struct batch {
			std::wstring text;
			std::vector<bookmark> marks;
			long rate = 0;          // the program's plus the fragment's
			int volume = 100;       // the fragment's share of the program's
			int inflection = 1;     // the chip's 2-bit input
			ULONG src_offset = 0;
			bool spell = false;
			bool flows = false;     // the next batch follows with no STOP
		};
		std::vector<batch> batches;
		{
			batch cur;
			auto flush = [&](bool flows) {
				if (!cur.text.empty() || !cur.marks.empty()) {
					cur.flows = flows && !cur.text.empty();
					batches.push_back(std::move(cur));
				}
				cur = batch();
			};
			for (const SPVTEXTFRAG *f = frags; f; f = f->pNext) {
				switch (f->State.eAction) {
				case SPVA_Bookmark:
					if (f->pTextStart)
						cur.marks.push_back({ std::wstring(f->pTextStart,
							f->ulTextLen ? f->ulTextLen : wcslen(f->pTextStart)),
							cur.text.size() });
					break;
				case SPVA_Speak:
				case SPVA_SpellOut: {
					if (!f->pTextStart || !f->ulTextLen)
						break;
					const SPVSTATE &st = f->State;
					long rate = std::clamp(site_rate + std::clamp(st.RateAdj, -10L, 10L), -10L, 10L);
					int volume = site_volume * std::min<int>(st.Volume, 100) / 100;
					if (old_markup) {
						rate = st.RateAdj ? std::clamp(st.RateAdj, -10L, 10L) : site_rate;
						volume = st.Volume != 100 ? std::min<int>(st.Volume, 100) : site_volume;
					}
					// The 2-bit inflection input, from SAPI's pitch adjustment.
					int inflection = std::clamp((int)st.PitchAdj.MiddleAdj / 6 + 1, 0, 3);
					// "spell": a control, a <spell> spoken as the word.
					bool spell = st.eAction == SPVA_SpellOut && !test_break("spell");
					if (!cur.text.empty()) {
						// A spelled fragment stands alone, closed on both
						// sides as it always was; a change of voice flows.
						if (spell || cur.spell)
							flush(false);
						else if (!old_markup && (rate != cur.rate || volume != cur.volume ||
						         inflection != cur.inflection))
							flush(true);
					}
					if (cur.text.empty()) {
						cur.rate = rate;
						cur.volume = volume;
						cur.inflection = inflection;
						cur.spell = spell;
						cur.src_offset = f->ulTextSrcOffset;
					} else if (!iswspace(cur.text.back()) && !iswspace(f->pTextStart[0])) {
						// Words arrive as separate fragments with no
						// whitespace; keep them from running together.
						cur.text += L' ';
					}
					cur.text.append(f->pTextStart, f->ulTextLen);
					break;
				}
				default:
					flush(false);
					break;
				}
			}
			flush(false);
		}

		// Where sentences begin, for a skip: at each batch's first word and
		// after each sentence's end in it; a spelled batch is one.
		struct place { size_t batch, chr; };
		std::vector<place> sentences;
		for (size_t i = 0; i < batches.size(); i++) {
			if (batches[i].text.empty())
				continue;
			if (batches[i].spell) {
				sentences.push_back({ i, 0 });
				continue;
			}
			std::vector<size_t> at;
			sentence_starts(batches[i].text, at);
			for (size_t chr : at)
				sentences.push_back({ i, chr });
		}

		// ---- Phase 2: synthesize and stream each batch ---------------
		ULONGLONG written = 0;
		std::vector<short> audio;
		size_t bi = 0, from = 0;    // the batch, and where in its text to begin
		bool follows = false;       // the batch before flowed into this one
		// Controls: every bookmark sent after all the audio; what Write
		// took never looked at.
		const bool marks_last = test_break("marks-end");
		const bool write_once = test_break("write-once");
		std::vector<std::wstring> held;

		// A skip (ISpVoice::Skip, a reading program's "next sentence"): so
		// many sentences on or back from the one at `chr` of batch `at`, 0
		// for its start.  As many are skipped as the text has; if that is
		// what was asked, speech goes on from there (true), else the call
		// ends (SAPI 5.4, TTS Engine Vendor Porting Guide).  Up to 1.1.1
		// every skip was answered with none done and the call ended.
		auto skip = [&](size_t at, size_t chr) -> bool {
			SPVSKIPTYPE type = SPVST_SENTENCE;
			long wanted = 0, did = 0;
			size_t cur = 0;
			const bool known = !test_break("skip") &&      // "skip": a control
				SUCCEEDED(site->GetSkipInfo(&type, &wanted)) &&
				type == SPVST_SENTENCE && !sentences.empty();
			if (known) {
				for (size_t k = 0; k < sentences.size(); k++)
					if (sentences[k].batch < at ||
					    (sentences[k].batch == at && sentences[k].chr <= chr))
						cur = k;
				did = std::clamp(wanted, -(long)cur, (long)(sentences.size() - 1 - cur));
			}
			site->CompleteSkip(did);
			if (!known || did != wanted)
				return false;
			bi = sentences[cur + did].batch;
			from = sentences[cur + did].chr;
			follows = false;
			return true;
		};

		while (bi < batches.size()) {
			batch &b = batches[bi];
			const size_t begin = from;
			from = 0;
			DWORD actions = site->GetActions();
			if (actions & SPVES_ABORT)
				return S_OK;
			if (actions & SPVES_SKIP) {
				if (!skip(bi, begin))
					return S_OK;
				continue;
			}

			if (b.text.empty()) {
				for (bookmark &m : b.marks) {
					if (marks_last)
						held.push_back(m.mark);
					else
						fire_bookmark(site, written, m.mark);
				}
				bi++;
				continue;
			}

			sentence_boundary(site, written, b.src_offset + (ULONG)begin);
			m_synth.utterance(b.text.substr(begin), b.rate, b.inflection, b.volume,
				b.spell, m_authentic, follows, !b.flows, audio);
			follows = false;

			// Bookmark byte thresholds, proportional to char position; the
			// ones before `begin` are behind a skip that landed past them.
			const BYTE *bytes = (const BYTE *)audio.data();
			const ULONGLONG total = (ULONGLONG)audio.size() * 2;
			const size_t chars = b.text.size() - begin;
			size_t mi = 0;
			while (mi < b.marks.size() && b.marks[mi].char_offset < begin)
				mi++;

			ULONGLONG pos = 0;
			bool moved = false;
			while (pos < total) {
				actions = site->GetActions();
				if (actions & SPVES_ABORT)
					return S_OK;
				if (actions & SPVES_SKIP) {
					// The batch was rendered whole, so the share of its
					// bytes SAPI took is the share of its text.
					if (!skip(bi, begin + (size_t)(pos * chars / total)))
						return S_OK;
					moved = true;
					break;
				}
				while (!marks_last && mi < b.marks.size() &&
				       pos >= (b.marks[mi].char_offset - begin) * total / chars) {
					fire_bookmark(site, written, b.marks[mi].mark);
					mi++;
				}
				// Bytes, not samples: a Write that takes an odd count must
				// not shift every sample after it by a byte.
				ULONG chunk = (ULONG)std::min<ULONGLONG>(total - pos, 4096);
				ULONG got = 0;
				HRESULT hr = site->Write(bytes + pos, chunk, &got);
				if (FAILED(hr))
					return hr;
				if (got > chunk || write_once)
					got = chunk;
				if (got == 0) {
					// Buffer full on Win7-era hosts: breathe and retry.
					Sleep(5);
					continue;
				}
				written += got;
				pos += got;
			}
			if (moved)
				continue;
			for (; mi < b.marks.size(); mi++) {
				if (marks_last)
					held.push_back(b.marks[mi].mark);
				else
					fire_bookmark(site, written, b.marks[mi].mark);
			}
			follows = b.flows;
			bi++;
		}
		for (const std::wstring &mark : held)
			fire_bookmark(site, written, mark);

		// Tail pad: hosts that stop the device when Speak() returns would
		// otherwise clip the final phoneme.  Offered until SAPI has all of
		// it, like the audio: up to 1.1.1 it was written once, so a host
		// that takes a part of a Write, or none while its buffer is full
		// (Windows 7), got a part of it or none.
		short pad[k_output_rate / 20] = {};   // 50 ms
		const BYTE *rest = (const BYTE *)pad;
		ULONG left = sizeof pad;
		while (left) {
			if (site->GetActions() & SPVES_ABORT)
				break;
			ULONG got = 0;
			if (FAILED(site->Write(rest, left, &got)))
				break;
			if (got > left || write_once)
				got = left;
			if (got == 0) {
				Sleep(5);
				continue;
			}
			rest += got;
			left -= got;
		}
		return S_OK;
	}

private:
	static void fire_bookmark(ISpTTSEngineSite *site, ULONGLONG offset,
	                          const std::wstring &mark)
	{
		auto *mem = (wchar_t *)CoTaskMemAlloc((mark.size() + 1) * sizeof(wchar_t));
		if (!mem)
			return;
		memcpy(mem, mark.c_str(), (mark.size() + 1) * sizeof(wchar_t));
		SPEVENT ev = {};
		ev.eEventId = SPEI_TTS_BOOKMARK;
		ev.elParamType = SPET_LPARAM_IS_STRING;
		ev.ullAudioStreamOffset = offset;
		ev.lParam = (LPARAM)mem;
		ev.wParam = (WPARAM)_wtol(mem);   // numeric ID, the NVDA convention
		site->AddEvents(&ev, 1);
	}

	static void sentence_boundary(ISpTTSEngineSite *site, ULONGLONG offset, ULONG src)
	{
		SPEVENT ev = {};
		ev.eEventId = SPEI_SENTENCE_BOUNDARY;
		ev.elParamType = SPET_LPARAM_IS_UNDEFINED;
		ev.ullAudioStreamOffset = offset;
		ev.lParam = (LPARAM)src;
		site->AddEvents(&ev, 1);
	}
};

//==========================================================================
// class factory and self-registration
//==========================================================================

class Factory : public IClassFactory
{
	LONG m_refs = 1;
public:
	Factory() { InterlockedIncrement(&g_objects); }
	~Factory() { InterlockedDecrement(&g_objects); }
	STDMETHODIMP QueryInterface(REFIID iid, void **out)
	{
		if (!out)
			return E_POINTER;
		*out = nullptr;
		if (iid != IID_IUnknown && iid != IID_IClassFactory)
			return E_NOINTERFACE;
		*out = this;
		AddRef();
		return S_OK;
	}
	STDMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_refs); }
	STDMETHODIMP_(ULONG) Release()
	{
		ULONG n = InterlockedDecrement(&m_refs);
		if (!n)
			delete this;
		return n;
	}
	STDMETHODIMP CreateInstance(IUnknown *outer, REFIID iid, void **out)
	{
		if (outer)
			return CLASS_E_NOAGGREGATION;
		Engine *e = new Engine;
		HRESULT hr = e->QueryInterface(iid, out);
		e->Release();
		return hr;
	}
	STDMETHODIMP LockServer(BOOL lock)
	{
		InterlockedExchangeAdd(&g_objects, lock ? 1 : -1);
		return S_OK;
	}
};

STDAPI DllCanUnloadNow() { return g_objects ? S_FALSE : S_OK; }

STDAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **out)
{
	if (clsid != CLSID_VotraxSC01)
		return CLASS_E_CLASSNOTAVAILABLE;
	Factory *f = new Factory;
	HRESULT hr = f->QueryInterface(iid, out);
	f->Release();
	return hr;
}

static void set_value(HKEY key, const wchar_t *name, const std::wstring &value)
{
	RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)value.c_str(),
		(DWORD)((value.size() + 1) * sizeof(wchar_t)));
}

static HRESULT do_register(bool add)
{
	wchar_t cls[64];
	StringFromGUID2(CLSID_VotraxSC01, cls, 64);
	std::wstring clsid_key = L"Software\\Classes\\CLSID\\" + std::wstring(cls);
	std::wstring tokens = L"SOFTWARE\\Microsoft\\Speech\\Voices\\Tokens\\";

	if (!add) {
		for (const variant_info &vi : k_variants)
			RegDeleteTreeW(HKEY_LOCAL_MACHINE, (tokens + vi.token).c_str());
		RegDeleteTreeW(HKEY_LOCAL_MACHINE, clsid_key.c_str());
		return S_OK;
	}

	// The COM class.
	HKEY key;
	if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, (clsid_key + L"\\InprocServer32").c_str(),
			0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr))
		return SELFREG_E_CLASS;
	set_value(key, nullptr, module_dir() + L"\\votrax_sapi.dll");
	set_value(key, L"ThreadingModel", L"Both");
	RegCloseKey(key);

	// One voice token per mask revision whose ROM is present beside the
	// DLL.  Re-running regsvr32 after adding a ROM adds its voice.
	for (const variant_info &vi : k_variants) {
		std::vector<unsigned char> rom;
		if (!read_file(module_dir() + L"\\" + vi.rom, rom))
			continue;
		std::wstring tok = tokens + vi.token;
		if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, tok.c_str(), 0, nullptr, 0,
				KEY_WRITE, nullptr, &key, nullptr))
			continue;
		set_value(key, nullptr, vi.display);
		set_value(key, L"CLSID", cls);
		RegCloseKey(key);
		if (!RegCreateKeyExW(HKEY_LOCAL_MACHINE, (tok + L"\\Attributes").c_str(),
				0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr)) {
			set_value(key, L"Name", vi.display);
			set_value(key, L"Language", L"409");
			set_value(key, L"Gender", L"Male");
			set_value(key, L"Age", L"Adult");
			set_value(key, L"Vendor", L"Votrax (emulated by MAME core)");
			set_value(key, L"VotraxVariant",
				vi.variant == VX_SC01 ? L"sc01" : L"sc01a");
			RegCloseKey(key);
		}
	}
	return S_OK;
}

STDAPI DllRegisterServer() { return do_register(true); }
STDAPI DllUnregisterServer() { return do_register(false); }
