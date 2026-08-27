# license:BSD-3-Clause
# copyright-holders:tgeczy
"""Physics tests for the SOLA time stretcher: no ROM needed.

What they pin: speed changes duration by the right ratio, pitch (measured
as zero-crossing rate) stays put, and unity speed is a passthrough.
"""

import ctypes
import math
import os
import sys

DLL = os.environ.get("SC01_DLL", os.path.join(
	os.path.dirname(__file__), "..", "build", "x64", "sc01.dll"))

lib = ctypes.CDLL(os.path.abspath(DLL))
lib.vxs_stretch_buffer.restype = ctypes.c_int
lib.vxs_stretch_buffer.argtypes = [ctypes.POINTER(ctypes.c_int16), ctypes.c_int,
	ctypes.c_double, ctypes.POINTER(ctypes.c_int16), ctypes.c_int]

RATE = 40000


def sine(freq, seconds):
	n = int(RATE * seconds)
	buf = (ctypes.c_int16 * n)()
	for i in range(n):
		buf[i] = int(20000 * math.sin(2 * math.pi * freq * i / RATE))
	return buf, n


def stretch(buf, n, speed):
	out = (ctypes.c_int16 * (n * 4 + 8192))()
	m = lib.vxs_stretch_buffer(buf, n, speed, out, len(out))
	return list(out[:m])


def crossings_per_sample(data):
	c = sum(1 for i in range(1, len(data)) if (data[i - 1] < 0) != (data[i] < 0))
	return c / len(data)


def main():
	failures = 0
	buf, n = sine(400, 1.0)
	base_rate = crossings_per_sample(list(buf))

	for speed, lo, hi in ((2.0, 0.40, 0.60), (0.5, 1.80, 2.20), (1.5, 0.57, 0.77)):
		out = stretch(buf, n, speed)
		ratio = len(out) / n
		if not (lo < ratio < hi):
			failures += 1
			print(f"FAIL speed {speed}: length ratio {ratio:.2f}, wanted ({lo}, {hi})")
		zc = crossings_per_sample(out)
		if abs(zc - base_rate) / base_rate > 0.08:
			failures += 1
			print(f"FAIL speed {speed}: pitch drifted "
			      f"({zc:.5f} vs {base_rate:.5f} crossings/sample)")

	out = stretch(buf, n, 1.0)
	if len(out) != n:
		failures += 1
		print(f"FAIL unity speed: {len(out)} samples out of {n} (expected passthrough)")

	if failures:
		print(f"stretch: {failures} failure(s)")
		return 1
	print("stretch: duration ratios, constant pitch and unity passthrough pass")
	return 0


if __name__ == "__main__":
	sys.exit(main())
