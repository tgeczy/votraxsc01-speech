// license:BSD-3-Clause
// copyright-holders:tgeczy
//
// sapi_harness.cpp -- the SAPI engine DLL driven the way SAPI drives it,
// with nothing registered: the DLL loaded from a path, its class factory
// taken from DllGetClassObject, the engine given a token (a stand-in whose
// Attributes key holds only VotraxVariant, which is all the engine reads),
// GetOutputFormat asked, and Speak called with SAPI's fragments and an
// engine site of our own.
//
// Why not SAPI itself: SAPI refuses a voice token kept under HKCU, so a
// test build could only reach it through machine-wide tokens -- which
// would sit beside, and could be mistaken for, the installed voice's.
// This drives the same COM object in the same way.
//
//   sapi_harness.exe <votrax_sapi.dll> <job file> <output folder> <sc01|sc01a> <authentic 0|1>
//
// One engine speaks the whole job, as one program's voice would, so the
// chip is carried from each request to the next.  The rate model goes to
// the test build through VOTRAX_SAPI_TEST_AUTHENTIC (the shipped DLL reads
// the registry and ignores it).
//
// The job: one request per line, UTF-8, tab-separated --
//     SAPI rate (-10..10), SAPI volume (0..100), VOTRAX_SAPI_TEST_BREAK
//     for this request ("-" for none), how the site takes writes, its
//     actions, then one field per fragment.
//   writes:    all | max=N (at most N bytes of each Write are taken) |
//              zero=K (each block is refused K times first: a Write that
//              takes nothing, Windows 7's full buffer)
//   actions:   - or a comma list of abort@W (SPVES_ABORT once W writes
//              were taken) and skip@W:N[:x] (SPVES_SKIP once W writes were
//              taken, N sentences; :x asks for a kind that is not
//              sentences)
//   fragments: S:rate:volume:middle:text (spoken), L:rate:volume:middle:text
//              (SPVA_SpellOut, a <spell>), B:name (a bookmark); rate is
//              the fragment's RateAdj, volume its Volume (100 when SAPI
//              saw no <volume>), middle its PitchAdj.MiddleAdj
//
// For each request the audio goes to <folder>\case_<k>.pcm and a line to
// stdout:
//     case <k> hr=<hex> rate=<declared Hz> bytes=<taken> writes=<taken>
//     calls=<every Write, taken or not> ms=<n> events=<each bookmark as
//     name@bytes taken when it came/its offset, or -> skips=<each
//     CompleteSkip's count, or ->

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <sperror.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// {F0C7A2B4-5C01-4D8E-A0B3-7E0193C1D2E4}, the engine's class
static const CLSID CLSID_VotraxSC01 =
	{ 0xf0c7a2b4, 0x5c01, 0x4d8e, { 0xa0, 0xb3, 0x7e, 0x01, 0x93, 0xc1, 0xd2, 0xe4 } };

static std::wstring wide(const std::string &s)
{
	int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
	std::wstring w(n, 0);
	if (n)
		MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
	return w;
}

// A token whose "Attributes" key is itself and holds only VotraxVariant.
class Token : public ISpObjectToken
{
	LONG m_refs = 1;
	std::wstring m_variant;
public:
	explicit Token(const std::wstring &variant) : m_variant(variant) {}
	STDMETHODIMP QueryInterface(REFIID iid, void **out)
	{
		if (!out)
			return E_POINTER;
		*out = nullptr;
		if (iid != IID_IUnknown && iid != IID_ISpDataKey && iid != IID_ISpObjectToken)
			return E_NOINTERFACE;
		*out = static_cast<ISpObjectToken *>(this);
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
	STDMETHODIMP GetStringValue(LPCWSTR name, LPWSTR *value)
	{
		if (!value)
			return E_POINTER;
		if (!name || wcscmp(name, L"VotraxVariant"))
			return SPERR_NOT_FOUND;
		size_t n = (m_variant.size() + 1) * sizeof(wchar_t);
		*value = (LPWSTR)CoTaskMemAlloc(n);
		if (!*value)
			return E_OUTOFMEMORY;
		memcpy(*value, m_variant.c_str(), n);
		return S_OK;
	}
	STDMETHODIMP OpenKey(LPCWSTR name, ISpDataKey **key)
	{
		if (!key)
			return E_POINTER;
		if (!name || wcscmp(name, L"Attributes"))
			return SPERR_NOT_FOUND;
		*key = this;
		AddRef();
		return S_OK;
	}
	STDMETHODIMP SetData(LPCWSTR, ULONG, const BYTE *) { return E_NOTIMPL; }
	STDMETHODIMP GetData(LPCWSTR, ULONG *, BYTE *) { return SPERR_NOT_FOUND; }
	STDMETHODIMP SetStringValue(LPCWSTR, LPCWSTR) { return E_NOTIMPL; }
	STDMETHODIMP SetDWORD(LPCWSTR, DWORD) { return E_NOTIMPL; }
	STDMETHODIMP GetDWORD(LPCWSTR, DWORD *) { return SPERR_NOT_FOUND; }
	STDMETHODIMP CreateKey(LPCWSTR, ISpDataKey **) { return E_NOTIMPL; }
	STDMETHODIMP DeleteKey(LPCWSTR) { return E_NOTIMPL; }
	STDMETHODIMP DeleteValue(LPCWSTR) { return E_NOTIMPL; }
	STDMETHODIMP EnumKeys(ULONG, LPWSTR *) { return SPERR_NO_MORE_ITEMS; }
	STDMETHODIMP EnumValues(ULONG, LPWSTR *) { return SPERR_NO_MORE_ITEMS; }
	STDMETHODIMP SetId(LPCWSTR, LPCWSTR, BOOL) { return E_NOTIMPL; }
	STDMETHODIMP GetId(LPWSTR *) { return E_NOTIMPL; }
	STDMETHODIMP GetCategory(ISpObjectTokenCategory **) { return E_NOTIMPL; }
	STDMETHODIMP CreateInstance(IUnknown *, DWORD, REFIID, void **) { return E_NOTIMPL; }
	STDMETHODIMP GetStorageFileName(REFCLSID, LPCWSTR, LPCWSTR, ULONG, LPWSTR *) { return E_NOTIMPL; }
	STDMETHODIMP RemoveStorageFileName(REFCLSID, LPCWSTR, BOOL) { return E_NOTIMPL; }
	STDMETHODIMP Remove(const CLSID *) { return E_NOTIMPL; }
	STDMETHODIMP IsUISupported(LPCWSTR, void *, ULONG, IUnknown *, BOOL *) { return E_NOTIMPL; }
	STDMETHODIMP DisplayUI(HWND, LPCWSTR, LPCWSTR, void *, ULONG, IUnknown *) { return E_NOTIMPL; }
	STDMETHODIMP MatchesAttributes(LPCWSTR, BOOL *) { return E_NOTIMPL; }
};

// SAPI's side of a Speak: the rate and volume, the writes (all of each, a
// part, or none for a while), the events, a purge and a skip request.
class Site : public ISpTTSEngineSite
{
	LONG m_refs = 1;
public:
	long rate = 0;
	USHORT volume = 100;
	int max_take = 0, refuse = 0, refused = 0;
	int abort_after = 0, skip_after = 0;
	long skip_count = 0;
	bool skip_other = false, skip_done = false;
	int writes = 0, calls = 0;
	std::vector<BYTE> audio;
	std::vector<std::string> events;
	std::vector<long> skips;

	STDMETHODIMP QueryInterface(REFIID iid, void **out)
	{
		if (!out)
			return E_POINTER;
		*out = nullptr;
		if (iid != IID_IUnknown && iid != IID_ISpEventSink && iid != IID_ISpTTSEngineSite)
			return E_NOINTERFACE;
		*out = static_cast<ISpTTSEngineSite *>(this);
		AddRef();
		return S_OK;
	}
	STDMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_refs); }
	STDMETHODIMP_(ULONG) Release() { return InterlockedDecrement(&m_refs); }   // on the stack
	STDMETHODIMP AddEvents(const SPEVENT *ev, ULONG n)
	{
		for (ULONG k = 0; k < n; k++) {
			if (ev[k].eEventId != SPEI_TTS_BOOKMARK ||
			    ev[k].elParamType != SPET_LPARAM_IS_STRING || !ev[k].lParam)
				continue;
			char name[64], line[160];
			WideCharToMultiByte(CP_UTF8, 0, (const wchar_t *)ev[k].lParam, -1,
				name, sizeof name, nullptr, nullptr);
			_snprintf(line, sizeof line, "%s@%lu/%llu", name,
				(unsigned long)audio.size(), ev[k].ullAudioStreamOffset);
			events.push_back(line);
		}
		return S_OK;
	}
	STDMETHODIMP GetEventInterest(ULONGLONG *interest)
	{
		if (interest)
			*interest = ~0ull;
		return S_OK;
	}
	STDMETHODIMP_(DWORD) GetActions()
	{
		DWORD a = abort_after && writes >= abort_after ? SPVES_ABORT : 0;
		if (skip_after && !skip_done && writes >= skip_after)
			a |= SPVES_SKIP;
		return a;
	}
	STDMETHODIMP Write(const void *data, ULONG n, ULONG *done)
	{
		ULONG take = n;
		calls++;
		if (refuse && refused < refuse) {
			refused++;      // the buffer is full: nothing is taken
			take = 0;
		} else {
			refused = 0;
			if (max_take && take > (ULONG)max_take)
				take = (ULONG)max_take;
		}
		audio.insert(audio.end(), (const BYTE *)data, (const BYTE *)data + take);
		if (take)
			writes++;
		if (done)
			*done = take;
		return S_OK;
	}
	STDMETHODIMP GetRate(long *r)
	{
		if (r)
			*r = rate;
		return S_OK;
	}
	STDMETHODIMP GetVolume(USHORT *v)
	{
		if (v)
			*v = volume;
		return S_OK;
	}
	STDMETHODIMP GetSkipInfo(SPVSKIPTYPE *type, long *count)
	{
		if (type)
			*type = skip_other ? (SPVSKIPTYPE)(SPVST_SENTENCE + 7) : SPVST_SENTENCE;
		if (count)
			*count = skip_count;
		return S_OK;
	}
	STDMETHODIMP CompleteSkip(long count)
	{
		skips.push_back(count);
		skip_done = true;
		return S_OK;
	}
};

static std::vector<std::string> split(const std::string &s, char c)
{
	std::vector<std::string> out;
	size_t a = 0, b;
	while ((b = s.find(c, a)) != std::string::npos) {
		out.push_back(s.substr(a, b - a));
		a = b + 1;
	}
	out.push_back(s.substr(a));
	return out;
}

int wmain(int argc, wchar_t **argv)
{
	if (argc < 6) {
		fputs("sapi_harness.exe <votrax_sapi.dll> <job> <folder> <sc01|sc01a> <authentic 0|1>\n", stderr);
		return 2;
	}
	CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	SetEnvironmentVariableW(L"VOTRAX_SAPI_TEST_AUTHENTIC", argv[5]);
	HMODULE dll = LoadLibraryExW(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!dll) {
		fprintf(stderr, "cannot load %ls (%lu)\n", argv[1], GetLastError());
		return 1;
	}
	typedef HRESULT (STDAPICALLTYPE *get_class_object)(REFCLSID, REFIID, void **);
	auto get = (get_class_object)GetProcAddress(dll, "DllGetClassObject");
	IClassFactory *factory = nullptr;
	if (!get || FAILED(get(CLSID_VotraxSC01, IID_IClassFactory, (void **)&factory))) {
		fputs("no Votrax SC-01 class in that DLL\n", stderr);
		return 1;
	}
	FILE *job = _wfopen(argv[2], L"rb");
	if (!job) {
		fputs("no job file\n", stderr);
		return 1;
	}
	std::string all;
	char buf[4096];
	size_t got;
	while ((got = fread(buf, 1, sizeof buf, job)) > 0)
		all.append(buf, got);
	fclose(job);

	ISpTTSEngine *engine = nullptr;
	ISpObjectWithToken *with = nullptr;
	Token *token = new Token(argv[4]);
	HRESULT made = factory->CreateInstance(nullptr, IID_ISpTTSEngine, (void **)&engine);
	if (SUCCEEDED(made))
		made = engine->QueryInterface(IID_ISpObjectWithToken, (void **)&with);
	if (SUCCEEDED(made))
		made = with->SetObjectToken(token);

	int k = 0;
	for (const std::string &line0 : split(all, '\n')) {
		std::string line = line0;
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		std::vector<std::string> f = split(line, '\t');
		if (f.size() < 6)
			continue;
		SetEnvironmentVariableA("VOTRAX_SAPI_TEST_BREAK", f[2] == "-" ? nullptr : f[2].c_str());

		Site site;
		site.rate = atol(f[0].c_str());
		site.volume = (USHORT)atoi(f[1].c_str());
		if (!f[3].compare(0, 4, "max="))
			site.max_take = atoi(f[3].c_str() + 4);
		if (!f[3].compare(0, 5, "zero="))
			site.refuse = atoi(f[3].c_str() + 5);
		for (const std::string &a : split(f[4], ',')) {
			if (!a.compare(0, 6, "abort@"))
				site.abort_after = atoi(a.c_str() + 6);
			if (!a.compare(0, 5, "skip@")) {
				std::vector<std::string> p = split(a.substr(5), ':');
				site.skip_after = atoi(p[0].c_str());
				site.skip_count = p.size() > 1 ? atol(p[1].c_str()) : 0;
				site.skip_other = p.size() > 2 && p[2] == "x";
			}
		}

		std::vector<SPVTEXTFRAG> frags(f.size() - 5);
		std::vector<std::wstring> texts(frags.size());
		memset(&frags[0], 0, frags.size() * sizeof frags[0]);
		for (size_t i = 0; i < frags.size(); i++) {
			const std::string &g = f[5 + i];
			SPVTEXTFRAG &x = frags[i];
			x.State.Volume = 100;
			if (g[0] == 'B') {
				x.State.eAction = SPVA_Bookmark;
				texts[i] = wide(g.substr(2));
			} else {    // S or L, then rate:volume:middle:text (the text may hold colons)
				size_t a = 2, b = g.find(':', a), c = g.find(':', b + 1), d = g.find(':', c + 1);
				x.State.eAction = g[0] == 'L' ? SPVA_SpellOut : SPVA_Speak;
				x.State.RateAdj = atol(g.substr(a, b - a).c_str());
				x.State.Volume = (USHORT)atoi(g.substr(b + 1, c - b - 1).c_str());
				x.State.PitchAdj.MiddleAdj = atol(g.substr(c + 1, d - c - 1).c_str());
				texts[i] = wide(g.substr(d + 1));
			}
		}
		ULONG src = 0;
		for (size_t i = 0; i < frags.size(); i++) {    // the strings no longer move
			frags[i].pTextStart = texts[i].c_str();
			frags[i].ulTextLen = (ULONG)texts[i].size();
			frags[i].ulTextSrcOffset = src;
			src += frags[i].ulTextLen + 1;
			frags[i].pNext = i + 1 < frags.size() ? &frags[i + 1] : nullptr;
		}

		HRESULT hr = made;
		GUID fmt;
		WAVEFORMATEX *wfx = nullptr;
		if (SUCCEEDED(hr))
			hr = engine->GetOutputFormat(nullptr, nullptr, &fmt, &wfx);
		DWORD t0 = GetTickCount();
		if (SUCCEEDED(hr))
			hr = engine->Speak(SPF_DEFAULT, fmt, wfx, &frags[0], &site);
		DWORD ms = GetTickCount() - t0;

		wchar_t out[MAX_PATH];
		_snwprintf(out, MAX_PATH, L"%ls\\case_%d.pcm", argv[3], k);
		FILE *o = _wfopen(out, L"wb");
		if (o) {
			if (!site.audio.empty())
				fwrite(site.audio.data(), 1, site.audio.size(), o);
			fclose(o);
		}
		std::string events, skips;
		for (size_t i = 0; i < site.events.size(); i++)
			events += (i ? "," : "") + site.events[i];
		for (size_t i = 0; i < site.skips.size(); i++)
			skips += (i ? "," : "") + std::to_string(site.skips[i]);
		printf("case %d hr=%08lx rate=%lu bytes=%lu writes=%d calls=%d ms=%lu events=%s skips=%s\n",
			k, (unsigned long)hr, wfx ? (unsigned long)wfx->nSamplesPerSec : 0ul,
			(unsigned long)site.audio.size(), site.writes, site.calls, (unsigned long)ms,
			events.empty() ? "-" : events.c_str(), skips.empty() ? "-" : skips.c_str());
		fflush(stdout);
		if (wfx)
			CoTaskMemFree(wfx);
		k++;
	}
	if (with)
		with->Release();
	if (engine)
		engine->Release();
	token->Release();
	factory->Release();
	return 0;
}
