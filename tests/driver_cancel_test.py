# license:BSD-3-Clause
# copyright-holders:tgeczy
"""Headless cancel test for the NVDA driver: no NVDA, real sc01.dll.

The bug this pins is the one bscross32 heard -- rapid tabbing makes a
piece of the previous utterance play at the head of the next.  Its cause
is audio fed to the player AFTER a cancel has stopped it: feed() returns
before playback finishes, so a stale block starts sounding before any
later stop() can discard it.  The driver must therefore never feed once
its epoch has moved.  A mock player with a feed delay makes the window
wide enough to hit on purpose; the test asserts zero feeds land after a
cancel, both for one well-timed cancel and for many at varied phases
(the way NVDA cancels on every keystroke).

usage: python tests/driver_cancel_test.py [path-to-sc01-x64.dll]
"""
import ctypes
import os
import shutil
import sys
import tempfile
import time
import types

HERE = os.path.dirname(__file__)
REPO = os.path.dirname(HERE)
DLL = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, "build", "x64", "sc01.dll")
ROMS = os.path.join(REPO, "roms")


class MockPlayer:
	"""Records feeds and stops.  feed() sleeps to imitate real playback
	taking real time, and tags each feed with whether a stop preceded it."""

	def __init__(self, channels, samplesPerSec, bitsPerSample, outputDevice=None):
		self.rate = samplesPerSec
		self.feeds = 0
		self.feedsAfterStop = 0
		self.stopped = 0
		self.feedDelay = 0.0
		self.closed = False
		self.total = 0

	def feed(self, data, size=None, onDone=None):
		if self.stopped:
			self.feedsAfterStop += 1
		self.feeds += 1
		self.total += len(data) // 2
		if self.feedDelay:
			time.sleep(self.feedDelay)
		if onDone:
			onDone()

	def idle(self):
		pass

	def stop(self):
		self.stopped += 1

	def pause(self, switch):
		pass

	def close(self):
		self.closed = True


class _AutoPropertyMeta(type):
	"""NVDA turns _get_x/_set_x into a property x; the driver relies on
	`synth.rate = v` calling _set_rate, so the stub must too."""
	def __new__(mcls, name, bases, ns):
		cls = super().__new__(mcls, name, bases, ns)
		names = set()
		for klass in cls.__mro__:
			for key in vars(klass):
				if key.startswith(("_get_", "_set_")):
					names.add(key[5:])
		for n in names:
			if isinstance(getattr(cls, n, None), property):
				continue
			setattr(cls, n, property(getattr(cls, "_get_" + n, None),
			                         getattr(cls, "_set_" + n, None)))
		return cls


class DriverSetting:
	def __init__(self, id, displayNameWithAccelerator="", availableInSettingsRing=False,
			defaultVal=None, displayName=None, useConfig=True):
		self.id = id
		self.defaultVal = defaultVal


class NumericDriverSetting(DriverSetting):
	def __init__(self, id, dn="", availableInSettingsRing=False, defaultVal=50,
			minVal=0, maxVal=100, minStep=1, normalStep=5, largeStep=10,
			displayName=None, useConfig=True):
		super().__init__(id, dn, availableInSettingsRing, defaultVal)
		self.minVal, self.maxVal, self.minStep = minVal, maxVal, minStep
		self.normalStep = max(normalStep, minStep)


class BooleanDriverSetting(DriverSetting):
	def __init__(self, id, dn="", availableInSettingsRing=False, displayName=None,
			defaultVal=False, useConfig=True):
		super().__init__(id, dn, availableInSettingsRing, defaultVal)


class VoiceInfo:
	def __init__(self, id, displayName, language=None):
		self.id, self.displayName, self.language = id, displayName, language


class _BaseSynthDriver(metaclass=_AutoPropertyMeta):
	@classmethod
	def VoiceSetting(cls):
		return DriverSetting("voice")

	@classmethod
	def RateSetting(cls, minStep=1):
		return NumericDriverSetting("rate", minStep=minStep)

	@classmethod
	def PitchSetting(cls, minStep=1):
		return NumericDriverSetting("pitch", minStep=minStep)

	def __init__(self):
		pass


class _Notifier:
	def __init__(self):
		self.events = []

	def notify(self, **kw):
		self.events.append(kw)


synthIndexReached = _Notifier()
synthDoneSpeaking = _Notifier()


class IndexCommand:
	def __init__(self, index):
		self.index = index


class CharacterModeCommand:
	def __init__(self, state):
		self.state = state


class PitchCommand:
	def __init__(self, offset=0):
		self.offset = offset


def energy(data):
	import array
	a = array.array("h")
	a.frombytes(bytes(data))
	return max((abs(x) for x in a), default=0)


def check_chip_silence(drv):
	"""The core of the tabbing bug, tested without threads: a chip left
	voicing a cancelled phone must be silenced before the next utterance
	renders, or its remainder leaks into that utterance's start."""
	failures = 0
	synth = drv.SynthDriver()
	# Stop the speak thread but keep the chip alive, so the chip can be
	# driven deterministically from here.
	synth._queue.put(None)
	synth._thread.join(2)
	try:
		block = max(1, int(synth._chip.sample_rate * 0.012))
		synth._chip_epoch = 7
		# Voice a vowel (0x21 = AY, as chip_test uses) and render across its
		# duration; the phone holds the chip busy, so it is still sounding.
		synth._chip.write(0x21)
		hot = bytearray()
		for _ in range(4):
			hot += synth._chip.render(block)
		hot_e = energy(hot)
		# A cancel bumped the epoch; syncing to it must silence the chip
		# even though the phone would otherwise keep voicing.
		synth._sync_chip_epoch(8)
		quiet = bytearray()
		for _ in range(4):
			quiet += synth._chip.render(block)
		quiet_e = energy(quiet)
		if hot_e < 1000:
			failures += 1
			print(f"FAIL chip-silence setup: vowel produced no sound ({hot_e})")
		if quiet_e > max(200, hot_e // 8):
			failures += 1
			print(f"FAIL chip-silence: {quiet_e} energy after reset "
			      f"(vs {hot_e} while voicing) -- the cancelled phone leaks")
		if not synth._chip.ready():
			failures += 1
			print("FAIL chip-silence: chip not ready after reset")
	finally:
		synth.terminate()
	return failures


def check_truncation_rate(drv):
	"""The new default rate: phone truncation must change tempo (fewer
	samples at higher speed) at constant pitch -- the clean, no-SOLA path."""
	failures = 0
	synth = drv.SynthDriver()
	synth._queue.put(None)
	synth._thread.join(2)
	try:
		phones = list(synth._chip.translate("testing one two three four"))

		def samples_at(speed):
			synth._authentic = False
			synth._speed = speed
			synth._epoch = 100
			synth._chip_epoch = 100        # skip the reset path; isolate tempo
			synth._chip.reset()
			synth._player.total = 0
			synth._feed_truncated(phones, 100)
			return synth._player.total

		slow = samples_at(1.0)
		fast = samples_at(2.0)
		if slow <= 0 or fast <= 0:
			failures += 1
			print(f"FAIL truncation: produced no audio ({slow}, {fast})")
		elif not (0.4 < fast / slow < 0.65):
			failures += 1
			print(f"FAIL truncation: speed 2.0 gave {fast/slow:.2f} of speed 1.0 "
			      f"(wanted ~0.5) -- tempo not tracking")
	finally:
		synth.terminate()
	return failures


def f0(data):
	"""Pitch of 16-bit mono audio at 40 kHz, by autocorrelation over the
	loudest 50 ms, decimated to 10 kHz (50-250 Hz search)."""
	import array
	a = array.array("h")
	a.frombytes(bytes(data))
	x = a[::4]
	w = 500
	start = max(range(0, max(1, len(x) - w), 50),
		key=lambda k: sum(abs(v) for v in x[k:k + w]), default=0)
	seg = x[start:start + w]
	best, lag_best = None, 0
	for lag in range(40, 200):
		c = sum(seg[i] * seg[i + lag] for i in range(w - lag))
		if best is None or c > best:
			best, lag_best = c, lag
	return 10000.0 / lag_best


def check_capital_pitch(drv):
	"""NVDA's capital pitch change: a PitchCommand offset around a capital
	raises it (one inflection level for the default 30), and the offset ends
	with the utterance -- the next one is back at the user's pitch."""
	failures = 0
	synth = drv.SynthDriver()
	try:
		audio = bytearray()
		real_feed = synth._player.feed

		def capture(data, *a, **k):
			audio.extend(data)
			return real_feed(data, *a, **k)

		synth._player.feed = capture
		results = []
		for seq in (["b"], [PitchCommand(30), "b", PitchCommand()], ["b"]):
			audio.clear()
			synthDoneSpeaking.events.clear()
			synth.speak([CharacterModeCommand(True)] + seq + [CharacterModeCommand(False)])
			for _ in range(100):
				if synthDoneSpeaking.events:
					break
				time.sleep(0.02)
			results.append(f0(audio))
		plain, capital, after = results
		if not capital > plain * 1.08:
			failures += 1
			print(f"FAIL capital pitch: {capital:.0f} Hz vs {plain:.0f} Hz plain (wanted higher)")
		if abs(after - plain) > plain * 0.04:
			failures += 1
			print(f"FAIL capital pitch leaked: {after:.0f} Hz after vs {plain:.0f} Hz before")
	finally:
		synth.terminate()
	return failures


def install_stubs(configdir):
	import builtins
	if not hasattr(builtins, "_"):
		builtins._ = lambda s: s     # NVDA installs gettext's _ as a builtin

	def mod(name, **attrs):
		m = types.ModuleType(name)
		for k, v in attrs.items():
			setattr(m, k, v)
		sys.modules[name] = m
		return m

	mod("config", conf={"audio": {"outputDevice": "default"}},
		getUserDefaultConfigPath=lambda: configdir)
	mod("nvwave", WavePlayer=MockPlayer)
	mod("logHandler", log=types.SimpleNamespace(
		info=lambda *a, **k: None, error=lambda *a, **k: None,
		warning=lambda *a, **k: None, debug=lambda *a, **k: None,
		debugWarning=lambda *a, **k: None))
	pkg = types.ModuleType("autoSettingsUtils")
	pkg.__path__ = []
	sys.modules["autoSettingsUtils"] = pkg
	mod("autoSettingsUtils.driverSetting", DriverSetting=DriverSetting,
		NumericDriverSetting=NumericDriverSetting, BooleanDriverSetting=BooleanDriverSetting)
	mod("synthDriverHandler", SynthDriver=_BaseSynthDriver, VoiceInfo=VoiceInfo,
		synthIndexReached=synthIndexReached, synthDoneSpeaking=synthDoneSpeaking)
	speech = types.ModuleType("speech")
	speech.__path__ = []
	sys.modules["speech"] = speech
	mod("speech.commands", IndexCommand=IndexCommand, CharacterModeCommand=CharacterModeCommand,
		PitchCommand=PitchCommand)


def main():
	if not os.path.isfile(DLL):
		print(f"missing DLL: {DLL} (build it first: .\\build.ps1 -Target dll)")
		return 2
	if not (os.path.isfile(os.path.join(ROMS, "sc01.bin"))):
		print("skip: no ROMs in roms/ -- cancel test needs a chip to render")
		return 0

	stage = tempfile.mkdtemp(prefix="votrax_nvda_")
	configdir = os.path.join(stage, "config")
	os.makedirs(configdir)
	drvdir = os.path.join(stage, "synthDrivers", "votraxsc01")
	os.makedirs(drvdir)
	shutil.copy(os.path.join(REPO, "nvda-addon", "synthDrivers", "votraxsc01", "__init__.py"), drvdir)
	shutil.copy(DLL, os.path.join(drvdir, "sc01-x64.dll"))
	for rom in ("sc01.bin", "sc01a.bin"):
		shutil.copy(os.path.join(ROMS, rom), drvdir)

	install_stubs(configdir)
	sys.path.insert(0, stage)
	import synthDrivers.votraxsc01 as drv

	failures = 0
	failures += check_chip_silence(drv)
	failures += check_truncation_rate(drv)
	failures += check_capital_pitch(drv)

	synth = drv.SynthDriver()
	try:
		# One well-timed cancel at the slowest rate (widest window).
		synth.rate = 0
		synth._player.feedDelay = 0.02
		time.sleep(0.05)                       # let a rate change settle
		synthDoneSpeaking.events.clear()
		synth.speak(["This is a deliberately long and slow utterance that will be "
			"cancelled partway through, to check that nothing is fed afterwards."])
		time.sleep(0.4)
		t0 = time.perf_counter()
		synth.cancel()
		cancel_ms = (time.perf_counter() - t0) * 1000
		at_cancel = synth._player.feeds
		time.sleep(0.5)
		stale = synth._player.feeds - at_cancel
		if cancel_ms >= 150:
			failures += 1
			print(f"FAIL cancel slow: returned in {cancel_ms:.0f} ms (wanted < 150)")
		if stale != 0:
			failures += 1
			print(f"FAIL cancel slow: {stale} feed(s) after the cancel")
		if synthDoneSpeaking.events:
			failures += 1
			print(f"FAIL cancel slow: reported done for a cancelled utterance")

		# Many cancels at varied phases -- the keystroke-tabbing case.
		synth._player.feedDelay = 0.008
		stale = 0
		for i in range(24):
			synth.speak([f"Utterance number {i} which will be interrupted partway through."])
			time.sleep(0.01 + (i % 7) * 0.010)
			synth.cancel()
			after = synth._player.feeds
			time.sleep(0.10)
			stale += synth._player.feeds - after
		if stale != 0:
			failures += 1
			print(f"FAIL repeated cancels: {stale} stale feed(s) of {synth._player.feeds} total")
		if synth._player.stopped == 0:
			failures += 1
			print("FAIL: cancel never stopped the player")

		# Speech still works after all that.
		synth._player.feedDelay = 0.0
		synth.rate = 50
		synthDoneSpeaking.events.clear()
		synth.speak(["Speech works again after cancelling."])
		time.sleep(0.6)
		if not synthDoneSpeaking.events:
			failures += 1
			print("FAIL: no audio produced after the cancel storm")
	finally:
		synth.terminate()

	if failures:
		print(f"driver_cancel: {failures} failure(s)")
		return 1
	print("driver_cancel: no stale feeds after cancel, prompt cancel, recovers, "
	      "capital pitch -- pass")
	return 0


if __name__ == "__main__":
	sys.exit(main())
