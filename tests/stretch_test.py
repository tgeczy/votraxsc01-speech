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

	# Regression: streaming feed at speeds past (FRAME+SEARCH)/HOP ~ 1.54,
	# where the analysis head can overshoot the input buffer.  Unclamped,
	# the trim's length went negative and the memmove killed the host
	# process (observed live: NVDA rate 100 crashed, rate 80 did not).
	# The driver's real pattern: many ~12 ms blocks, pulling as we go.
	lib.vxs_create.restype = ctypes.c_void_p
	lib.vxs_destroy.argtypes = [ctypes.c_void_p]
	lib.vxs_set_speed.argtypes = [ctypes.c_void_p, ctypes.c_double]
	lib.vxs_feed.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int16), ctypes.c_int]
	lib.vxs_pull.restype = ctypes.c_int
	lib.vxs_pull.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int16), ctypes.c_int]
	for speed in (1.6, 2.0, 3.0):
		s = lib.vxs_create()
		lib.vxs_set_speed(s, speed)
		got = 0
		block = (ctypes.c_int16 * 480)(*([1000] * 480))
		pull = (ctypes.c_int16 * 4096)()
		for _ in range(300):                      # ~3.6 s of feed
			lib.vxs_feed(s, block, 480)
			while True:
				m = lib.vxs_pull(s, pull, 4096)
				got += m
				if not m:
					break
		lib.vxs_destroy(s)
		want = 300 * 480 / speed
		if not (0.8 * want < got < 1.2 * want):
			failures += 1
			print(f"FAIL streaming speed {speed}: {got} samples, wanted ~{int(want)}")

	# Isolation + onset: the streaming bugs bscross32 reported (a bit of the
	# previous utterance leaking into the next, word onsets clipped) come
	# down to state surviving between utterances and a fade-in on the first
	# frame.  vxs_flush must leave the stretcher exactly as fresh, and the
	# first output frame must be the input verbatim, not faded up.
	lib.vxs_flush.argtypes = [ctypes.c_void_p]
	OVERLAP = 256

	def burst(freq, seconds, amp=15000):
		n = int(RATE * seconds)
		a = (ctypes.c_int16 * n)()
		for i in range(n):
			a[i] = int(amp * math.sin(2 * math.pi * freq * i / RATE))
		return a, n

	def drain(s):
		o, pull = [], (ctypes.c_int16 * 8192)()
		while True:
			m = lib.vxs_pull(s, pull, 8192)
			o.extend(pull[:m])
			if not m:
				break
		return o

	def utter(s, buf, n):
		lib.vxs_feed(s, buf, n)
		o = drain(s)
		lib.vxs_flush(s)
		return o + drain(s)

	A, An = burst(180, 0.20)
	B, Bn = burst(320, 0.20)

	s = lib.vxs_create()
	lib.vxs_set_speed(s, 1.5)
	utter(s, A, An)                 # a whole first utterance, then flushed
	reusedB = utter(s, B, Bn)       # the second, on the reused stretcher
	lib.vxs_destroy(s)

	s = lib.vxs_create()
	lib.vxs_set_speed(s, 1.5)
	freshB = utter(s, B, Bn)        # the same utterance, fresh stretcher
	lib.vxs_destroy(s)

	if reusedB != freshB:
		failures += 1
		print(f"FAIL isolation: B after a flushed utterance ({len(reusedB)} "
		      f"samples) differs from B fresh ({len(freshB)}) -- state leaked")

	s = lib.vxs_create()
	lib.vxs_set_speed(s, 1.5)
	lib.vxs_feed(s, B, Bn)
	onset = drain(s)
	lib.vxs_destroy(s)
	if onset[:OVERLAP] != list(B[:OVERLAP]):
		failures += 1
		print("FAIL onset: first frame is not the input verbatim "
		      "(faded up from silence)")

	if failures:
		print(f"stretch: {failures} failure(s)")
		return 1
	print("stretch: duration ratios, constant pitch and unity passthrough pass")
	return 0


if __name__ == "__main__":
	sys.exit(main())
