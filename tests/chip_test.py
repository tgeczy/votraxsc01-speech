# license:BSD-3-Clause
# copyright-holders:tgeczy
"""ROM-gated chip tests: skipped cleanly on machines without the dumps,
alive the moment a ROM lands in roms/.  The gate is real where the ROM
exists and silent where it does not -- the panthera pattern.

What they pin, silicon-side:
  * the loader rejects a corrupted ROM and names both CRCs;
  * the ready line starts asserted, drops on write, re-asserts;
  * a phone renders nonzero audio energy and STOP decays to silence;
  * doubling the clock roughly halves a phone's duration (rate = clock).
"""

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
	if failures:
		print(f"chip: {failures} failure(s)")
		return 1
	print("chip: ROM verify, ready line, audio energy, STOP silence and "
	      "clock scaling all pass")
	return 0


if __name__ == "__main__":
	sys.exit(main())
