# license:BSD-3-Clause
# copyright-holders:tgeczy
"""ROM-gated chip tests: skipped cleanly on machines without the dumps,
alive the moment a ROM lands in roms/.  The gate is real where the ROM
exists and silent where it does not -- the panthera pattern.

What they pin, silicon-side:
  * the loader rejects a corrupted ROM and names both CRCs;
  * the ready line starts asserted, drops on write, re-asserts;
  * a phone renders nonzero audio energy and STOP decays to silence;
  * doubling the clock roughly halves a phone's duration (rate = clock);
  * vx_min_hold reads each phone's ROM delays (S: tick 8 + 3 of 16), and
    an S held that long hisses where a plain half-length cut is silent;
  * the closure fix (src/chip/votrax.cpp): off renders upstream MAME bit
    for bit (pinned by a fingerprint taken from the unmodified file), on
    gives K and P a release burst ahead of the vowel, which upstream
    lacks -- so the burst check fails on upstream and passes on the fix.
"""

import hashlib
import struct

import ctypes
import os
import sys

HERE = os.path.dirname(__file__)
DLL = os.environ.get("SC01_DLL", os.path.join(HERE, "..", "build", "x64", "sc01.dll"))
ROMS = os.path.join(HERE, "..", "roms")


def load():
	lib = ctypes.CDLL(os.path.abspath(DLL))
	p = ctypes.c_void_p
	lib.vx_create.restype = p
	lib.vx_create.argtypes = [ctypes.c_int, ctypes.c_uint, ctypes.c_char_p,
		ctypes.c_uint, ctypes.c_char_p, ctypes.c_size_t]
	lib.vx_destroy.argtypes = [p]
	lib.vx_set_clock.argtypes = [p, ctypes.c_uint]
	lib.vx_sample_rate.restype = ctypes.c_double
	lib.vx_sample_rate.argtypes = [p]
	lib.vx_write.argtypes = [p, ctypes.c_ubyte]
	lib.vx_ready.restype = ctypes.c_int
	lib.vx_ready.argtypes = [p]
	lib.vx_render.restype = ctypes.c_int
	lib.vx_render.argtypes = [p, ctypes.POINTER(ctypes.c_int16), ctypes.c_int]
	lib.vx_closure_fix.argtypes = [p, ctypes.c_int]
	lib.vx_min_hold.restype = ctypes.c_int
	lib.vx_min_hold.argtypes = [p, ctypes.c_ubyte]
	return lib


def render(lib, chip, count):
	buf = (ctypes.c_int16 * count)()
	lib.vx_render(chip, buf, count)
	return list(buf)


def phone_samples(lib, chip, phone, cap):
	"""Write a phone, render until ready re-asserts; return the samples."""
	lib.vx_write(chip, phone)
	out = []
	while not lib.vx_ready(chip) and len(out) < cap:
		out += render(lib, chip, 256)
	return out


# Phone codes by name, from votrax.cpp's s_phone_table.
PHONES = ("EH3 EH2 EH1 PA0 DT A1 A2 ZH AH2 I3 I2 I1 M N B V CH SH Z AW1 NG AH1 "
	"OO1 OO L K J H G F D S A AY Y1 UH3 AH P O I U Y T R E W AE AE1 AW2 UH2 "
	"UH1 UH O2 O1 IU U1 THV TH ER EH E1 AW PA1 STOP").split()

# Rendered by the UNMODIFIED third_party/mame/votrax.cpp (the build before
# src/chip existed): SHA-256 of speak(STOCK_PHRASE) as little-endian int16.
STOCK_PHRASE = "PA1 K A1 AY Y PA0 S P E1 Y T CH PA0 T AE1 EH3 K PA0 B I3 DT UH3 N PA1"
STOCK_SHA256 = {
	1: "84ad7fedb0162353dd7a0b615b23c9839212375afacfc6ba9a567e64dce4e9f4",  # SC-01A
	0: "155e8ff669e6a9664ccfe9254a40010a7d80632e62723f3d6469d8ad315dfc90",  # SC-01
}


def speak(lib, chip, names):
	"""Write each phone when ready; end with STOP and 100 ms of tail."""
	out = []
	for name in names.split():
		out += phone_samples(lib, chip, PHONES.index(name), 200000)
	lib.vx_write(chip, 0x3F)
	out += render(lib, chip, 4000)
	return out


def burst_lead_ms(lib, rom, variant, fix, word):
	"""Milliseconds between the first audible sample of a stop-initial word
	and the vowel reaching full voice: the release burst, if any."""
	chip = lib.vx_create(variant, 720000, rom, len(rom), None, 0)
	lib.vx_closure_fix(chip, fix)
	phone_samples(lib, chip, PHONES.index("PA1"), 200000)
	s = speak(lib, chip, word)
	lib.vx_destroy(chip)
	first = next(i for i, v in enumerate(s) if abs(v) > 200)
	loud = next(i for i, v in enumerate(s) if abs(v) > 4000)
	return (loud - first) / 40.0


def main():
	rom_path = None
	variant = 1
	for name, var in (("sc01a.bin", 1), ("sc01.bin", 0)):
		path = os.path.join(ROMS, name)
		if os.path.isfile(path):
			rom_path, variant = path, var
			break
	if not rom_path:
		print("chip: SKIPPED (no ROM in roms/; tests activate when one lands)")
		return 0

	lib = load()
	rom = open(rom_path, "rb").read()
	err = ctypes.create_string_buffer(256)

	failures = 0

	# A corrupted ROM must be refused, with both CRCs in the message.
	bad = bytearray(rom)
	bad[0] ^= 0xFF
	if lib.vx_create(variant, 720000, bytes(bad), len(bad), err, 256):
		failures += 1
		print("FAIL corrupted ROM was accepted")
	elif b"CRC" not in err.value:
		failures += 1
		print(f"FAIL rejection message lacks CRCs: {err.value!r}")

	chip = lib.vx_create(variant, 720000, rom, len(rom), err, 256)
	if not chip:
		print(f"FAIL create: {err.value!r}")
		return 1

	rate = lib.vx_sample_rate(chip)
	if abs(rate - 40000.0) > 1:
		failures += 1
		print(f"FAIL sample rate {rate}, wanted 40000")

	# Ready line etiquette.
	if not lib.vx_ready(chip):
		failures += 1
		print("FAIL ready not asserted at reset")
	lib.vx_write(chip, 0x21)          # AY
	if lib.vx_ready(chip):
		failures += 1
		print("FAIL ready still asserted right after a write")

	# A vowel makes noise; STOP decays to silence.
	samples = []
	while not lib.vx_ready(chip) and len(samples) < 200000:
		samples += render(lib, chip, 256)
	energy = max(abs(s) for s in samples) if samples else 0
	if energy < 500:
		failures += 1
		print(f"FAIL phone AY peak {energy}; expected audible signal")
	lib.vx_write(chip, 0x3F)          # STOP
	render(lib, chip, 40000)          # a second to settle
	tail = render(lib, chip, 4000)
	if max(abs(s) for s in tail) > 50:
		failures += 1
		print(f"FAIL not silent after STOP (peak {max(abs(s) for s in tail)})")

	# Rate is the clock: doubling it should roughly halve a phone's length.
	n_720 = len(phone_samples(lib, chip, 0x21, 200000))
	lib.vx_set_clock(chip, 1440000)
	n_1440 = len(phone_samples(lib, chip, 0x21, 200000))
	lib.vx_set_clock(chip, 720000)
	if not n_720 or not n_1440:
		failures += 1
		print("FAIL phone timing render came back empty")
	else:
		ratio = (n_720 / 40000.0) / (n_1440 / 80000.0)
		if not (1.5 < ratio < 2.7):
			failures += 1
			print(f"FAIL clock scaling ratio {ratio:.2f}, expected ~2")

	lib.vx_destroy(chip)

	# Minimum hold for rate-by-truncation: the later ROM delay plus three
	# ticks, a tick being (dur*4+1) chip updates of two samples.  S: dur
	# 29, delays 2/8 -> 11 ticks; AH: dur 76, delays 4/2 -> 7 ticks.
	chip = lib.vx_create(variant, 720000, rom, len(rom), err, 256)
	for name, want in (("S", 11 * 117 * 2), ("AH", 7 * 305 * 2)):
		got = lib.vx_min_hold(chip, PHONES.index(name))
		if got != want:
			failures += 1
			print(f"FAIL min hold {name}: {got} samples, want {want}")

	def hiss(hold):
		"""High-frequency energy of an S held `hold` samples, then a pause."""
		c = lib.vx_create(variant, 720000, rom, len(rom), None, 0)
		lib.vx_write(c, PHONES.index("S"))
		s = render(lib, c, hold)
		lib.vx_write(c, PHONES.index("PA0"))
		s += render(lib, c, 4000)
		lib.vx_destroy(c)
		return sum((b - a) ** 2 for a, b in zip(s, s[1:]))
	nat_s = len(phone_samples(lib, chip, PHONES.index("S"), 200000))
	cut, held = hiss(nat_s // 2), hiss(lib.vx_min_hold(chip, PHONES.index("S")))
	if not held > 20 * max(cut, 1):
		failures += 1
		print(f"FAIL an S held to its minimum should hiss far louder than one cut "
		      f"in half: held {held}, cut {cut}")
	lib.vx_destroy(chip)

	# Closure fix off = upstream MAME, bit for bit.
	chip = lib.vx_create(variant, 720000, rom, len(rom), err, 256)
	lib.vx_closure_fix(chip, 0)
	s = speak(lib, chip, STOCK_PHRASE)
	lib.vx_destroy(chip)
	got = hashlib.sha256(struct.pack(f"<{len(s)}h", *s)).hexdigest()
	if got != STOCK_SHA256[variant]:
		failures += 1
		print(f"FAIL closure fix off no longer renders upstream MAME: {got}")

	# Closure fix on: K and P get their release burst (upstream: ~1 ms).
	for word in ("K A1 AY Y", "P E1 Y"):
		on = burst_lead_ms(lib, rom, variant, 1, word)
		off = burst_lead_ms(lib, rom, variant, 0, word)
		if on < 20 or off > 3:
			failures += 1
			print(f"FAIL burst before vowel in {word!r}: fix on {on:.0f} ms "
			      f"(want >= 20), off {off:.0f} ms (want <= 3)")

	if failures:
		print(f"chip: {failures} failure(s)")
		return 1
	print("chip: ROM verify, ready line, audio energy, STOP silence, "
	      "clock scaling, minimum hold, upstream fingerprint and stop bursts "
	      "all pass")
	return 0


if __name__ == "__main__":
	sys.exit(main())
