# license:BSD-3-Clause
# copyright-holders:tgeczy
"""Run the real NVDA driver headlessly and capture what it hands nvwave.

The driver (nvda-addon/synthDrivers/votraxsc01/__init__.py) is imported
under the same NVDA stubs tests/driver_cancel_test.py uses, with a player
that records every byte fed and then applies NVDA's one change to speech
audio: leading-silence trimming (on by default, config speech.
trimLeadingSilence).  Per nvdaHelper/local/wasapi.cpp, after an idle or a
stop the stream skips samples until the first one above 1/1024 of full
scale (|x| > 32 for 16-bit), then inserts one frame of silence.  NVDA does
no filtering, resampling or DC removal of its own (Windows may resample
to the device's mix format).

  python tools/nvda_capture.py out_dir [--rate 50] [--voice sc01a]
        [--spell] [--authentic] TEXT [TEXT ...]

Each TEXT becomes out_dir/NN_<text>.wav at the chip's native rate, as
NVDA would play it.  Prints where each utterance's sound starts.
"""
import argparse
import os
import shutil
import sys
import tempfile
import threading
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "tests"))
_argv, sys.argv = sys.argv, sys.argv[:1]  # the harness reads argv[1] as a DLL path
import driver_cancel_test as harness     # the NVDA stubs, shared
sys.argv = _argv


class CapturePlayer:
	"""nvwave.WavePlayer stand-in: records the stream NVDA would play."""

	def __init__(self, channels, samplesPerSec, bitsPerSample, outputDevice=None, **kw):
		self.rate = samplesPerSec
		self.data = bytearray()
		self.trimming = True          # NVDA re-arms trimming after idle()/stop()
		self.total = 0                # the cancel test's counter, kept compatible
		self.feeds = self.stopped = self.feedsAfterStop = 0
		self.feedDelay = 0.0

	def feed(self, data, size=None, onDone=None):
		data = bytes(data if size is None else data[:size])
		self.feeds += 1
		self.total += len(data) // 2
		if self.trimming:
			# skip leading samples at or under 1/1024 of full scale
			k = 0
			while k + 1 < len(data) and abs(int.from_bytes(data[k:k + 2], "little", signed=True)) <= 32:
				k += 2
			if k + 1 < len(data):
				self.trimming = False
				self.data += b"\0\0"          # wasapi.cpp inserts one silent frame
				self.data += data[k:]
		else:
			self.data += data
		if onDone:
			onDone()

	def idle(self):
		self.trimming = True

	def stop(self):
		self.stopped += 1
		self.trimming = True

	def pause(self, switch):
		pass

	def close(self):
		pass


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("out")
	ap.add_argument("text", nargs="+")
	ap.add_argument("--rate", type=int, default=50)
	ap.add_argument("--voice", default=None)
	ap.add_argument("--spell", action="store_true")
	ap.add_argument("--authentic", action="store_true")
	a = ap.parse_args()
	os.makedirs(a.out, exist_ok=True)

	stage = tempfile.mkdtemp(prefix="votrax_capture_")
	configdir = os.path.join(stage, "config")
	os.makedirs(configdir)
	drvdir = os.path.join(stage, "synthDrivers", "votraxsc01")
	os.makedirs(drvdir)
	shutil.copy(os.path.join(REPO, "nvda-addon", "synthDrivers", "votraxsc01", "__init__.py"), drvdir)
	shutil.copy(harness.DLL, os.path.join(drvdir, "sc01-x64.dll"))
	for rom in ("sc01.bin", "sc01a.bin"):
		shutil.copy(os.path.join(harness.ROMS, rom), drvdir)
	harness.install_stubs(configdir)
	sys.modules["nvwave"].WavePlayer = CapturePlayer
	sys.path.insert(0, stage)
	import synthDrivers.votraxsc01 as drv

	synth = drv.SynthDriver()
	try:
		if a.voice:
			synth.voice = a.voice
		if a.authentic:
			synth.authenticRate = True
		synth.rate = a.rate
		done = threading.Event()
		harness.synthDoneSpeaking.notify = lambda **kw: done.set()
		for n, text in enumerate(a.text, 1):
			done.clear()
			synth._player.data = bytearray()
			synth._player.trimming = True
			seq = [harness.CharacterModeCommand(True), text, harness.CharacterModeCommand(False)] if a.spell else [text]
			synth.speak(seq + [harness.IndexCommand(n)])
			if not done.wait(20):
				print(f"timeout on {text!r}")
			player = synth._player
			safe = "".join(c if c.isalnum() else "_" for c in text)[:30]
			path = os.path.join(a.out, f"{n:02d}_{safe}.wav")
			with wave.open(path, "wb") as w:
				w.setnchannels(1)
				w.setsampwidth(2)
				w.setframerate(player.rate)
				w.writeframes(bytes(player.data))
			print(f"{path}: {len(player.data) // 2 / player.rate * 1000:.0f} ms at {player.rate} Hz")
	finally:
		synth.terminate()


if __name__ == "__main__":
	main()
