// license:BSD-3-Clause
// copyright-holders:tgeczy
//
// emu.h -- a miniature stand-in for MAME's emulator environment, just wide
// enough to compile an unmodified MAME sound device outside MAME.
//
// Scope: exactly the API surface that third_party/mame/votrax.cpp and
// votrax.h touch, measured by grep before this file was written:
//
//   device_t / device_sound_interface lifecycle  (start, reset, clock)
//   one sound_stream                             (samples, put, update)
//   one emu_timer + attotime                     (adjust, expire, remaining)
//   required_memory_region                       (base)
//   devcb_write_line                             (operator(), bind)
//   ROM_START family macros, tiny_rom_entry
//   bitswap, save_item, line_state, u8..u64
//
// The semantic inversion vs. real MAME: there is no scheduler.  The host
// calls device_t::shim_render(), which advances emulated time sample by
// sample and fires the timer at its exact position inside the block.  All
// stream->update() calls therefore become no-ops: control writes are only
// legal between render calls, and the render loop is always "caught up".
//
// THREADING CONTRACT: one device = one thread at a time.  Interleave
// control writes and shim_render() from the same thread, or serialize.

#pragma once

#include <cstdint>
#include <cstring>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

//--------------------------------------------------------------------------
// Fixed-width aliases, as MAME spells them.
//--------------------------------------------------------------------------
using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s8  = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

// Digital line states, as devcb and the AR line use them.
enum line_state { CLEAR_LINE = 0, ASSERT_LINE = 1 };

// Cold-path attribute; only an optimizer hint in MAME, safe to drop.
#define ATTR_COLD

//--------------------------------------------------------------------------
// bitswap -- MAME's bit extractor.  The FIRST listed bit position becomes
// the MOST significant bit of the result.
//--------------------------------------------------------------------------
template <typename T, typename... B>
constexpr u64 bitswap(T val, B... bits)
{
	u64 result = 0;
	((result = (result << 1) | ((u64(val) >> bits) & 1u)), ...);
	return result;
}

//--------------------------------------------------------------------------
// attotime -- emulated absolute time.  Double seconds are exact enough
// here: resolution stays far below one master-clock tick for any session
// length a speech synthesizer will ever see.
//--------------------------------------------------------------------------
class attotime
{
public:
	constexpr attotime() : m_seconds(0.0), m_never(false) {}
	explicit constexpr attotime(double secs) : m_seconds(secs), m_never(false) {}

	static attotime never() { attotime t; t.m_never = true; return t; }
	static attotime from_ticks(u64 ticks, u32 hz) { return attotime(double(ticks) / double(hz)); }

	bool is_never() const { return m_never; }
	double as_double() const { return m_seconds; }
	u64 as_ticks(u32 hz) const { return u64(std::llround(m_seconds * double(hz))); }

private:
	double m_seconds;
	bool m_never;
};

//--------------------------------------------------------------------------
// sound_stream -- one mono output stream, rendered in host-driven blocks.
//--------------------------------------------------------------------------
class sound_stream
{
public:
	using sample_t = float;

	int samples() const { return m_samples; }
	void put(int channel, int index, sample_t value) { (void)channel; m_buffer[index] = value; }

	// The host render loop is always caught up, so a mid-write flush has
	// nothing to do.  See the semantic-inversion note at the top.
	void update() {}

	void set_sample_rate(double rate) { m_rate = rate; }
	double sample_rate() const { return m_rate; }

	// -- shim side, used by device_t::shim_render --
	void shim_attach(sample_t *buffer, int count) { m_buffer = buffer; m_samples = count; }

private:
	sample_t *m_buffer = nullptr;
	int m_samples = 0;
	double m_rate = 0.0;
};

//--------------------------------------------------------------------------
// emu_timer -- a single one-shot timer, fired by the render loop.
//--------------------------------------------------------------------------
class device_t;

class emu_timer
{
public:
	// Real MAME's adjust() also defaults param to 0; device_clock_changed()
	// in votrax.cpp relies on that default, so keep it faithfully.
	void adjust(attotime delta, s32 param = 0);

	attotime expire() const { return m_armed ? attotime(m_expire) : attotime::never(); }
	attotime remaining() const;
	s32 param() const { return m_param; }

	// -- shim side --
	std::function<void(s32)> shim_callback;
	device_t *shim_owner = nullptr;
	double m_expire = 0.0;
	s32 m_param = 0;
	bool m_armed = false;
};

//--------------------------------------------------------------------------
// devcb_write_line -- output line callback.  The AR line uses this; hosts
// may either register a callback or poll request().
//--------------------------------------------------------------------------
class devcb_write_line
{
public:
	explicit devcb_write_line(device_t &) {}
	devcb_write_line &bind() { return *this; }
	void operator()(int state) { if (m_fn) m_fn(state); }
	void shim_set(std::function<void(int)> fn) { m_fn = std::move(fn); }

private:
	std::function<void(int)> m_fn;
};

//--------------------------------------------------------------------------
// ROM declaration macros.  They expand to a static tiny_rom_entry table
// carrying name, length and hash text; the host supplies the actual bytes.
//--------------------------------------------------------------------------
struct tiny_rom_entry
{
	const char *name;      // file name (ROM_LOAD) or region tag (ROM_REGION)
	const char *hashdata;  // "c:<crc8hex> s:<sha1hex>" for ROM_LOAD entries
	u32 offset;
	u32 length;
	u32 flags;             // 1 = region, 2 = load, 0 = end
};

#define ROM_START(name) static const tiny_rom_entry rom_##name[] = {
#define ROM_REGION64_LE(length, tag, flags) { tag, nullptr, 0, length, 1 },
#define ROM_LOAD(name, offset, length, hash) { name, hash, offset, length, 2 },
#define ROM_END { nullptr, nullptr, 0, 0, 0 } };
#define ROM_NAME(name) rom_##name
#define CRC(x) "c:" #x
#define SHA1(x) " s:" #x

//--------------------------------------------------------------------------
// memory_region / required_memory_region -- the device's internal ROM.
//--------------------------------------------------------------------------
class memory_region
{
public:
	u8 *base() { return m_data.data(); }
	const u8 *base() const { return m_data.data(); }
	u32 bytes() const { return u32(m_data.size()); }
	void shim_fill(const u8 *data, u32 length) { m_data.assign(data, data + length); }

private:
	std::vector<u8> m_data;
};

class required_memory_region
{
public:
	required_memory_region(device_t &owner, const char *tag);
	memory_region *operator->() const { return m_region; }
	void shim_attach(memory_region *region) { m_region = region; }

private:
	memory_region *m_region = nullptr;
};

//--------------------------------------------------------------------------
// device_type -- just a name pair; DEFINE/DECLARE keep MAME's spelling.
//--------------------------------------------------------------------------
struct device_type
{
	const char *shortname;
	const char *fullname;
};

#define DECLARE_DEVICE_TYPE(Type, Class) extern const device_type Type;
#define DEFINE_DEVICE_TYPE(Type, Class, Short, Full) const device_type Type{ Short, Full };

struct machine_config {};

// Timer plumbing macros, as the call sites spell them.
#define TIMER_CALLBACK_MEMBER(name) void name(s32 param)
#define FUNC(x) (&x)
#define NAME(x) (x)

//--------------------------------------------------------------------------
// device_t -- lifecycle plus the shim's host interface.
//--------------------------------------------------------------------------
class device_t
{
	friend class required_memory_region;
	friend class emu_timer;
	friend class device_sound_interface;

public:
	device_t(const machine_config &, device_type type, const char *tag, device_t *owner, u32 clock)
		: m_type(type), m_tag(tag ? tag : "device"), m_clock(clock)
	{
		(void)owner;
	}
	virtual ~device_t()
	{
		for (emu_timer *t : m_timers)
			delete t;
	}

	u32 clock() const { return m_clock; }
	const char *tag() const { return m_tag.c_str(); }

	// ---- host interface (not part of MAME's API) ----

	// Expected ROM metadata, so loaders can verify before starting.
	const tiny_rom_entry *shim_rom_region() const { return device_rom_region(); }

	// One-time start: install ROM bytes, then run device_start/device_reset.
	// Returns false (with *error set) if the ROM does not match the
	// device's declared region length.
	bool shim_start(const u8 *rom, u32 rom_length, std::string *error)
	{
		const tiny_rom_entry *re = device_rom_region();
		u32 want = 0;
		for (; re && re->flags; re++)
			if (re->flags == 1)
				want = re->length;
		if (want == 0 || rom_length != want) {
			if (error)
				*error = "ROM length mismatch: expected " + std::to_string(want)
				       + " bytes, got " + std::to_string(rom_length);
			return false;
		}
		m_region.shim_fill(rom, rom_length);
		if (m_rom_slot)
			m_rom_slot->shim_attach(&m_region);
		device_start();
		device_reset();
		return true;
	}

	void shim_reset() { device_reset(); }

	void shim_set_clock(u32 hz)
	{
		m_clock = hz;
		device_clock_changed();
	}

	double shim_now() const { return m_now; }
	sound_stream &shim_stream() { return m_stream_obj; }

	// device_sound_interface installs the bridge from the shim render loop
	// to the device's virtual sound_stream_update.
	void shim_set_stream_render(std::function<void()> fn) { m_stream_render = std::move(fn); }

	// Render `count` mono float samples, firing the timer at its exact
	// position within the block.  This is the only place emulated time
	// advances.
	void shim_render(sound_stream::sample_t *out, int count)
	{
		const double rate = m_stream_obj.sample_rate();
		const double dt = 1.0 / rate;
		int done = 0;
		while (done < count) {
			// Fire everything already due, then find the next deadline.
			double next_expire = -1.0;
			for (emu_timer *t : m_timers) {
				if (!t->m_armed)
					continue;
				if (t->m_expire <= m_now + dt * 0.5) {
					t->m_armed = false;   // one-shot; the callback may re-adjust
					t->shim_callback(t->m_param);
					next_expire = -2.0;   // rescan: the callback may have re-armed
					break;
				}
				if (next_expire < 0 || t->m_expire < next_expire)
					next_expire = t->m_expire;
			}
			if (next_expire == -2.0)
				continue;

			int n = count - done;
			if (next_expire >= 0) {
				int until = int(std::ceil((next_expire - m_now) / dt - 0.5));
				if (until < 1)
					until = 1;
				if (until < n)
					n = until;
			}
			m_stream_obj.shim_attach(out + done, n);
			m_stream_render();
			m_now += n * dt;
			done += n;
		}
	}

protected:
	// MAME device lifecycle, implemented by the vendored device.
	virtual void device_start() = 0;
	virtual void device_reset() {}
	virtual void device_clock_changed() {}
	virtual const tiny_rom_entry *device_rom_region() const { return nullptr; }

	template <typename D>
	emu_timer *timer_alloc(void (D::*fn)(s32), D *self)
	{
		emu_timer *t = new emu_timer;
		t->shim_owner = this;
		t->shim_callback = [self, fn](s32 param) { (self->*fn)(param); };
		m_timers.push_back(t);
		return t;
	}

	// Savestates do not exist outside MAME; registration is a no-op.
	template <typename T>
	void save_item(T &, const char * = nullptr) {}

private:
	device_type m_type;
	std::string m_tag;
	u32 m_clock;
	double m_now = 0.0;
	std::vector<emu_timer *> m_timers;
	memory_region m_region;
	required_memory_region *m_rom_slot = nullptr;
	sound_stream m_stream_obj;
	std::function<void()> m_stream_render;
};

inline required_memory_region::required_memory_region(device_t &owner, const char *tag)
{
	(void)tag;
	owner.m_rom_slot = this;
}

inline void emu_timer::adjust(attotime delta, s32 param)
{
	m_expire = shim_owner->shim_now() + delta.as_double();
	m_param = param;
	m_armed = !delta.is_never();
}

inline attotime emu_timer::remaining() const
{
	if (!m_armed)
		return attotime::never();
	double r = m_expire - shim_owner->shim_now();
	return attotime(r > 0 ? r : 0);
}

//--------------------------------------------------------------------------
// device_sound_interface -- binds the device's sound_stream_update to the
// shim render loop and hands out the single stream.
//--------------------------------------------------------------------------
class device_sound_interface
{
public:
	device_sound_interface(const machine_config &, device_t &device)
		: m_device(device)
	{
		// Virtual dispatch happens at call time, so binding `this` here --
		// before the derived device's constructor body runs -- is safe.
		device.shim_set_stream_render([this, &device]() {
			sound_stream_update(device.shim_stream());
		});
	}
	virtual ~device_sound_interface() = default;

	virtual void sound_stream_update(sound_stream &stream) = 0;

protected:
	sound_stream *stream_alloc(int inputs, int outputs, double sample_rate)
	{
		(void)inputs; (void)outputs;
		m_device.shim_stream().set_sample_rate(sample_rate);
		return &m_device.shim_stream();
	}

private:
	device_t &m_device;
};
