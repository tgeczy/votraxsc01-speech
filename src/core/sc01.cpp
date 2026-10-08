// license:BSD-3-Clause
// copyright-holders:tgeczy
//
// sc01.cpp -- implements the flat C API of sc01.h on top of the MAME
// device, built from src/chip/votrax.cpp: a marked copy of the vendored,
// unmodified third_party/mame/votrax.cpp carrying one closure-timing fix
// (switchable; off = upstream exactly).
//
// The expected ROM CRCs are not duplicated here: they are parsed out of
// the ROM_START blocks the MAME source itself declares, so the vendored
// file stays the single source of truth.

#include "emu.h"
#include "votrax.h"

#include "sc01.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

namespace {

// Standard reflected CRC-32 (the polynomial zlib and MAME use).
u32 crc32_bytes(const u8 *data, size_t len)
{
	u32 crc = 0xffffffffu;
	for (size_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (int b = 0; b < 8; b++)
			crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
	}
	return ~crc;
}

// Parse the "c:<8 hex digits>" clause out of a tiny_rom_entry hash string.
bool parse_crc(const char *hashdata, u32 *out)
{
	if (!hashdata)
		return false;
	const char *p = std::strstr(hashdata, "c:");
	if (!p)
		return false;
	u32 value = 0;
	for (int i = 0; i < 8; i++) {
		char c = p[2 + i];
		u32 digit;
		if (c >= '0' && c <= '9') digit = c - '0';
		else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
		else return false;
		value = (value << 4) | digit;
	}
	*out = value;
	return true;
}

machine_config g_config;

// Build a throwaway device just to read its declared ROM metadata.
u32 declared_crc(int variant)
{
	std::unique_ptr<votrax_sc01_device> dev;
	if (variant == VX_SC01A)
		dev = std::make_unique<votrax_sc01a_device>(g_config, "sc01a", nullptr, VX_DEFAULT_CLOCK);
	else
		dev = std::make_unique<votrax_sc01_device>(g_config, "sc01", nullptr, VX_DEFAULT_CLOCK);
	for (const tiny_rom_entry *re = dev->shim_rom_region(); re && re->flags; re++) {
		u32 crc;
		if (re->flags == 2 && parse_crc(re->hashdata, &crc))
			return crc;
	}
	return 0;
}

} // anonymous namespace

struct vx_chip {
	std::unique_ptr<votrax_sc01_device> device;
};

extern "C" {

vx_chip *vx_create(int variant, uint32_t clock_hz,
                   const uint8_t *rom, uint32_t rom_len,
                   char *error, size_t error_len)
{
	auto fail = [&](const char *msg) -> vx_chip * {
		if (error && error_len)
			snprintf(error, error_len, "%s", msg);
		return nullptr;
	};

	if (variant != VX_SC01 && variant != VX_SC01A)
		return fail("unknown variant: 0 = SC-01, 1 = SC-01A");
	if (!rom)
		return fail("no ROM supplied; the 512-byte internal mask ROM dump is required");
	if (rom_len != VX_ROM_SIZE) {
		char buf[128];
		snprintf(buf, sizeof buf, "ROM is %u bytes; the SC-01 internal ROM is exactly %u",
		         rom_len, (unsigned)VX_ROM_SIZE);
		return fail(buf);
	}

	u32 want = vx_expected_crc(variant);
	u32 got = crc32_bytes(rom, rom_len);
	if (want != 0 && got != want) {
		char buf[192];
		snprintf(buf, sizeof buf,
		         "ROM CRC mismatch for %s: expected %08x, file has %08x -- wrong file or wrong variant?",
		         variant == VX_SC01A ? "SC-01A" : "SC-01", want, got);
		return fail(buf);
	}

	auto chip = new vx_chip;
	if (variant == VX_SC01A)
		chip->device = std::make_unique<votrax_sc01a_device>(g_config, "sc01a", nullptr,
		                                                     clock_hz ? clock_hz : VX_DEFAULT_CLOCK);
	else
		chip->device = std::make_unique<votrax_sc01_device>(g_config, "sc01", nullptr,
		                                                    clock_hz ? clock_hz : VX_DEFAULT_CLOCK);

	std::string err;
	if (!chip->device->shim_start(rom, rom_len, &err)) {
		if (error && error_len)
			snprintf(error, error_len, "%s", err.c_str());
		delete chip;
		return nullptr;
	}
	return chip;
}

void vx_destroy(vx_chip *chip)
{
	delete chip;
}

void vx_reset(vx_chip *chip)
{
	chip->device->shim_reset();
}

void vx_set_clock(vx_chip *chip, uint32_t clock_hz)
{
	chip->device->shim_set_clock(clock_hz);
}

uint32_t vx_clock(const vx_chip *chip)
{
	return chip->device->clock();
}

double vx_sample_rate(const vx_chip *chip)
{
	return chip->device->shim_stream().sample_rate();
}

void vx_write(vx_chip *chip, uint8_t phone)
{
	chip->device->write(phone);
}

void vx_inflection(vx_chip *chip, uint8_t level)
{
	chip->device->inflection_w(level);
}

void vx_closure_fix(vx_chip *chip, int on)
{
	chip->device->set_closure_fix(on != 0);
}

int vx_ready(vx_chip *chip)
{
	return chip->device->request();
}

int vx_render(vx_chip *chip, int16_t *out, int count)
{
	// Chunked float render -> 16-bit conversion.  The MAME stream is
	// nominally within [-1, 1]; clamp defensively rather than wrap.
	sound_stream::sample_t buf[1024];
	int done = 0;
	while (done < count) {
		int n = std::min<int>(count - done, 1024);
		chip->device->shim_render(buf, n);
		for (int i = 0; i < n; i++) {
			float v = buf[i] * 30000.0f;
			if (v > 32767.0f) v = 32767.0f;
			if (v < -32768.0f) v = -32768.0f;
			out[done + i] = (int16_t)v;
		}
		done += n;
	}
	return done;
}

uint32_t vx_expected_crc(int variant)
{
	static u32 cached[2] = { 0, 0 };
	int idx = (variant == VX_SC01A) ? 1 : 0;
	if (!cached[idx])
		cached[idx] = declared_crc(variant);
	return cached[idx];
}

uint32_t vx_crc32(const uint8_t *data, size_t len)
{
	return crc32_bytes(data, len);
}

const char *vx_phone_name(uint8_t phone)
{
	// The table is private to the device; mirror the public contract by
	// keeping our own copy in one place, checked against MAME's by the
	// phone-table test in tools/say01.c (--names prints all 64).
	static const char *const names[64] = {
		"EH3", "EH2", "EH1", "PA0", "DT",  "A1",  "A2",  "ZH",
		"AH2", "I3",  "I2",  "I1",  "M",   "N",   "B",   "V",
		"CH",  "SH",  "Z",   "AW1", "NG",  "AH1", "OO1", "OO",
		"L",   "K",   "J",   "H",   "G",   "F",   "D",   "S",
		"A",   "AY",  "Y1",  "UH3", "AH",  "P",   "O",   "I",
		"U",   "Y",   "T",   "R",   "E",   "W",   "AE",  "AE1",
		"AW2", "UH2", "UH1", "UH",  "O2",  "O1",  "IU",  "U1",
		"THV", "TH",  "ER",  "EH",  "E1",  "AW",  "PA1", "STOP"
	};
	return phone < 64 ? names[phone] : nullptr;
}

int vx_phone_by_name(const char *name)
{
	if (!name)
		return -1;
	for (int i = 0; i < 64; i++) {
		const char *p = vx_phone_name((uint8_t)i);
		size_t j = 0;
		for (; p[j] && name[j]; j++) {
			char a = p[j], b = name[j];
			if (b >= 'a' && b <= 'z') b -= 32;
			if (a != b)
				break;
		}
		if (!p[j] && !name[j])
			return i;
	}
	return -1;
}

} // extern "C"
