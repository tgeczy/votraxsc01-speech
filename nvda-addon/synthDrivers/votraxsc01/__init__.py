# license:BSD-3-Clause
# copyright-holders:tgeczy
"""NVDA synth driver for the Votrax SC-01, via MAME's chip simulation.

Layout of the machinery, so a reader can hold it all at once:

  sc01.dll (ctypes)     -- the chip (vx_*) and text-to-phoneme (ttv_*)
  _Chip                 -- thin ctypes wrapper, owns one vx_chip
  SynthDriver           -- NVDA-facing class; turns speech sequences into
                           work items on a queue
  _speak_thread         -- the only place the chip is touched after
                           startup: it feeds phonemes when the chip asks,
                           renders audio blocks, reports indexes, and is
                           also where voice/rate changes are applied, so
                           the chip needs no locking at all

Cancellation is an epoch counter: cancel() bumps it, and speech items
carrying an older epoch are dropped.  Control items (rate, pitch, voice)
carry no epoch -- a settings change must survive a cancel.

Rate is the chip's master clock, exactly as on real Votrax hardware --
speeding up raises pitch too, and that is authentic, not a bug.  The
2-bit inflection input is exposed as NVDA's pitch setting, quantized to
its four real levels.
"""

import ctypes
import os
import queue
import threading

from autoSettingsUtils.driverSetting import BooleanDriverSetting
import config
import nvwave
from speech.commands import IndexCommand, CharacterModeCommand
from synthDriverHandler import SynthDriver as BaseSynthDriver, VoiceInfo, \
	synthIndexReached, synthDoneSpeaking

_DIR = os.path.dirname(__file__)

#: Phone code the chip idles on; also what we send to close an utterance.
_STOP = 0x3F

#: The chip's datasheet master clock.  Rate 50 maps here; the range below
#: spans half to double speed, which the silicon handles happily.
_BASE_CLOCK = 720000


def _dll_name():
	return "sc01-x64.dll" if ctypes.sizeof(ctypes.c_void_p) == 8 else "sc01-x86.dll"


def _output_device():
	# The output-device setting moved between config sections across the
	# NVDA versions this add-on spans.
	try:
		return config.conf["audio"]["outputDevice"]
	except KeyError:
		return config.conf["speech"]["outputDevice"]


class _Chip:
	"""One vx_chip behind ctypes, with the argtypes declared up front.

	Declaring argtypes is not optional detail: without them ctypes guesses,
	and a guessed 32-bit handle in a 64-bit process is a crash that only
	happens once the heap wanders past 4 GB.
	"""

	def __init__(self, variant, rom_bytes):
		self._lib = lib = ctypes.cdll.LoadLibrary(os.path.join(_DIR, _dll_name()))
		p = ctypes.c_void_p
		lib.vx_create.restype = p
		lib.vx_create.argtypes = [ctypes.c_int, ctypes.c_uint, ctypes.c_char_p,
			ctypes.c_uint, ctypes.c_char_p, ctypes.c_size_t]
		for name, res, args in (
			("vx_destroy", None, [p]),
			("vx_set_clock", None, [p, ctypes.c_uint]),
			("vx_sample_rate", ctypes.c_double, [p]),
			("vx_write", None, [p, ctypes.c_ubyte]),
			("vx_inflection", None, [p, ctypes.c_ubyte]),
			("vx_ready", ctypes.c_int, [p]),
			("vx_render", ctypes.c_int, [p, ctypes.POINTER(ctypes.c_int16), ctypes.c_int]),
			("ttv_translate", ctypes.c_int, [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]),
			("ttv_spell", ctypes.c_int, [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]),
			("vxs_create", p, []),
			("vxs_destroy", None, [p]),
			("vxs_reset", None, [p]),
			("vxs_set_speed", None, [p, ctypes.c_double]),
			("vxs_feed", None, [p, ctypes.c_char_p, ctypes.c_int]),
			("vxs_pull", ctypes.c_int, [p, ctypes.POINTER(ctypes.c_int16), ctypes.c_int]),
		):
			fn = getattr(lib, name)
			fn.restype, fn.argtypes = res, args

		err = ctypes.create_string_buffer(256)
		self._chip = lib.vx_create(variant, _BASE_CLOCK, rom_bytes,
			len(rom_bytes), err, 256)
		if not self._chip:
			raise RuntimeError(err.value.decode("ascii", "replace"))

	def close(self):
		if self._chip:
			self._lib.vx_destroy(self._chip)
			self._chip = None

	def set_clock(self, hz):
		self._lib.vx_set_clock(self._chip, int(hz))

	@property
	def sample_rate(self):
		return int(self._lib.vx_sample_rate(self._chip))

	def write(self, phone):
		self._lib.vx_write(self._chip, phone)

	def inflection(self, level):
		self._lib.vx_inflection(self._chip, level)

	def ready(self):
		return bool(self._lib.vx_ready(self._chip))

	def render(self, count):
		buf = (ctypes.c_int16 * count)()
		n = self._lib.vx_render(self._chip, buf, count)
		return ctypes.string_at(buf, n * 2)

	# -- the time stretcher lives in the same DLL; thin pass-throughs --

	def stretch_create(self):
		return self._lib.vxs_create()

	def stretch_destroy(self, s):
		self._lib.vxs_destroy(s)

	def stretch_reset(self, s):
		self._lib.vxs_reset(s)

	def stretch_speed(self, s, speed):
		self._lib.vxs_set_speed(s, speed)

	def stretch_feed(self, s, data):
		self._lib.vxs_feed(s, data, len(data) // 2)

	def stretch_pull(self, s, count):
		buf = (ctypes.c_int16 * count)()
		n = self._lib.vxs_pull(s, buf, count)
		return ctypes.string_at(buf, n * 2)

	def translate(self, text, spell=False):
		# ttv_* run the 1985 engine, which lives on globals and is NOT
		# reentrant -- but NVDA only ever calls speak() from its main
		# thread, and the speak thread never translates, so the contract
		# holds without a lock.  (The chip functions are a disjoint state.)
		out = ctypes.create_string_buffer(4096)
		fn = self._lib.ttv_spell if spell else self._lib.ttv_translate
		n = fn(text.encode("utf-8", "replace"), out, 4096)
		return bytes(out.raw[:n])


def _data_dir():
	"""The persistent ROM home: <NVDA user config>\\votrax-data.

	Add-on updates replace the add-on's own folder wholesale, so anything
	living only there is lost on every update.  A sibling of the config's
	synthDrivers folder survives updates, reinstalls and portable copies --
	the same pattern panthera uses for its speech data."""
	return os.path.join(config.getUserDefaultConfigPath() or _DIR, "votrax-data")


def _find_rom(filename):
	"""Search order: the persistent data folder, then the add-on folder
	(where release bundles carry the ROMs), then the legacy synthDrivers
	spot.  Returning None (not raising) lets check() stay quiet."""
	for base in (_data_dir(), _DIR,
			os.path.join(config.getUserDefaultConfigPath() or "", "synthDrivers")):
		path = os.path.join(base, filename)
		if os.path.isfile(path):
			return path
	return None


def _migrate_roms():
	"""Copy bundled ROMs into the persistent folder, once.

	This is what makes updates safe: the first run of a release bundle
	seeds votrax-data, and from then on the ROMs survive no matter what
	happens to the add-on folder.  Never overwrites -- a user-supplied
	ROM in the data folder always wins."""
	import shutil
	try:
		os.makedirs(_data_dir(), exist_ok=True)
		for _display, _variant, rom in _VOICES.values():
			bundled = os.path.join(_DIR, rom)
			target = os.path.join(_data_dir(), rom)
			if os.path.isfile(bundled) and not os.path.isfile(target):
				shutil.copy2(bundled, target)
	except OSError:
		pass   # a read-only config is not a reason to fail the synth


#: voice id -> (display name, variant number, rom file)
_VOICES = {
	"sc01": ("SC-01 (1980 mask)", 0, "sc01.bin"),
	"sc01a": ("SC-01-A (later mask)", 1, "sc01a.bin"),
}


class SynthDriver(BaseSynthDriver):
	name = "votraxsc01"
	# Translators: description of the Votrax speech synthesizer.
	description = _("Votrax SC-01 (emulated)")

	supportedSettings = (
		BaseSynthDriver.VoiceSetting(),
		BaseSynthDriver.RateSetting(),
		BaseSynthDriver.PitchSetting(minStep=25),   # four real hardware levels
		# Rate is constant-pitch time scaling by default.  This box brings
		# back the hardware truth: rate as master clock, where faster is
		# also higher -- the chipmunk the 1980 knob actually made.
		BooleanDriverSetting(
			"authenticRate",
			# Translators: a Votrax driver setting: rate varies the chip
			# clock, changing pitch with speed, as the real hardware did.
			_("&Authentic rate (vary the chip clock; pitch rises with speed)"),
			defaultVal=False,
		),
	)
	supportedCommands = {IndexCommand, CharacterModeCommand}
	supportedNotifications = {synthIndexReached, synthDoneSpeaking}

	@classmethod
	def check(cls):
		try:
			return any(_find_rom(rom) for _, _, rom in _VOICES.values())
		except Exception:
			return False

	def __init__(self):
		self._chip = None
		self._player = None
		self._player_rate = 0
		self._rate = 50
		self._pitch = 50
		self._authentic = False
		self._stretch = None
		self._stretch_epoch = -1
		self._epoch = 0
		self._queue = queue.Queue()
		_migrate_roms()
		self._voice = next(v for v in _VOICES if _find_rom(_VOICES[v][2]))
		# First open happens here on the main thread, before the speak
		# thread exists; afterwards the chip belongs to that thread only.
		self._open_voice(self._voice)
		self._thread = threading.Thread(target=self._speak_thread,
			name="votraxsc01", daemon=True)
		self._thread.start()

	def terminate(self):
		self.cancel()
		self._queue.put(None)
		self._thread.join(timeout=2)
		if self._player:
			self._player.close()
			self._player = None
		if self._stretch and self._chip:
			self._chip.stretch_destroy(self._stretch)
			self._stretch = None
		if self._chip:
			self._chip.close()
			self._chip = None

	# ---- chip lifecycle (called from the speak thread after startup) --

	def _open_voice(self, voice_id):
		display, variant, rom_name = _VOICES[voice_id]
		rom_path = _find_rom(rom_name)
		if not rom_path:
			raise RuntimeError("missing ROM %s" % rom_name)
		with open(rom_path, "rb") as f:
			rom = f.read()
		old = self._chip
		self._chip = _Chip(variant, rom)
		if self._stretch is None:
			self._stretch = self._chip.stretch_create()
		self._apply_rate()
		self._apply_pitch()
		self._ensure_player()
		self._voice = voice_id
		if old:
			old.close()

	def _ensure_player(self):
		rate = self._chip.sample_rate
		if self._player is None or self._player_rate != rate:
			old = self._player
			self._player = nvwave.WavePlayer(channels=1, samplesPerSec=rate,
				bitsPerSample=16, outputDevice=_output_device())
			self._player_rate = rate
			if old:
				old.close()

	def _apply_rate(self):
		# 0..100 -> half to double speed, exponentially, so equal slider
		# steps sound like equal speed ratios.  Two regimes:
		#   default   -- the chip runs at its datasheet clock and the
		#                stretcher changes speed at constant pitch;
		#   authentic -- rate IS the master clock, pitch and all, as the
		#                1980 hardware's one knob really behaved.
		factor = 2.0 ** ((self._rate - 50) / 50.0)
		if self._authentic:
			self._chip.set_clock(_BASE_CLOCK * factor)
			self._chip.stretch_speed(self._stretch, 1.0)
		else:
			self._chip.set_clock(_BASE_CLOCK)
			self._chip.stretch_speed(self._stretch, factor)

	def _apply_pitch(self):
		# Quantize NVDA's 0..100 onto the chip's four inflection levels.
		self._chip.inflection(min(3, self._pitch * 4 // 101))

	# ---- NVDA settings ----------------------------------------------
	# Set-methods only record the value and enqueue a control item; the
	# speak thread applies it.  Control items carry epoch None on purpose:
	# a settings change must survive a cancel.

	def _get_voice(self):
		return self._voice

	def _set_voice(self, value):
		if value in _VOICES and value != self._voice and _find_rom(_VOICES[value][2]):
			self._queue.put((None, "voice", value, None))

	def _getAvailableVoices(self):
		out = {}
		for vid, (display, _variant, rom) in _VOICES.items():
			if _find_rom(rom):
				out[vid] = VoiceInfo(vid, display, "en")
		return out

	def _get_rate(self):
		return self._rate

	def _set_rate(self, value):
		self._rate = max(0, min(100, value))
		self._queue.put((None, "rate", None, None))

	def _get_pitch(self):
		return self._pitch

	def _set_pitch(self, value):
		self._pitch = max(0, min(100, value))
		self._queue.put((None, "pitch", None, None))

	def _get_authenticRate(self):
		return self._authentic

	def _set_authenticRate(self, value):
		self._authentic = bool(value)
		self._queue.put((None, "rate", None, None))

	# ---- speaking ----------------------------------------------------

	def speak(self, speechSequence):
		epoch = self._epoch
		spell = False
		for item in speechSequence:
			if isinstance(item, str):
				phones = self._chip.translate(item, spell=spell)
				if phones:
					self._queue.put((epoch, "phones", phones, None))
			elif isinstance(item, IndexCommand):
				self._queue.put((epoch, "index", None, item.index))
			elif isinstance(item, CharacterModeCommand):
				spell = item.state
		self._queue.put((epoch, "done", None, None))

	def cancel(self):
		self._epoch += 1
		# Drain speech items but put surviving control items back: a voice
		# or rate change queued just before a cancel must still happen.
		keep = []
		try:
			while True:
				item = self._queue.get_nowait()
				if item and item[0] is None:
					keep.append(item)
		except queue.Empty:
			pass
		for item in keep:
			self._queue.put(item)
		if self._player:
			self._player.stop()

	def pause(self, switch):
		if self._player:
			self._player.pause(switch)

	# ---- the chip thread --------------------------------------------

	def _speak_thread(self):
		while True:
			item = self._queue.get()
			if item is None:
				return
			epoch, kind, payload, index = item
			if epoch is not None and epoch != self._epoch:
				continue
			if kind == "rate":
				self._apply_rate()
				self._ensure_player()
			elif kind == "pitch":
				self._apply_pitch()
			elif kind == "voice":
				self._open_voice(payload)
			elif kind == "index":
				synthIndexReached.notify(synth=self, index=index)
			elif kind == "phones":
				self._feed(payload, epoch)
			elif kind == "done":
				# Close the utterance the way the hardware would: STOP,
				# then a short fixed tail so the last phone rings out.
				self._feed(bytes([_STOP]), epoch)
				self._render_tail(epoch, seconds=0.25)
				if epoch == self._epoch:
					self._player.idle()
					synthDoneSpeaking.notify(synth=self)

	def _push_audio(self, data, epoch):
		"""Chip output to the player, through the stretcher by default.

		A new epoch drops whatever the stretcher was holding: after a
		cancel, half-processed old speech must not leak into the next
		utterance."""
		if self._stretch_epoch != epoch:
			self._chip.stretch_reset(self._stretch)
			self._stretch_epoch = epoch
		if self._authentic:
			self._player.feed(data)
			return
		self._chip.stretch_feed(self._stretch, data)
		while True:
			out = self._chip.stretch_pull(self._stretch, 4096)
			if not out:
				break
			self._player.feed(out)

	def _feed(self, phones, epoch):
		"""Feed phonemes as the chip asks for them, streaming the audio.

		Blocks of ~12 ms keep cancel latency low; the epoch check between
		blocks is what makes a cancel take effect mid-word.
		"""
		block = max(1, int(self._chip.sample_rate * 0.012))
		pending = list(phones)
		while pending and epoch == self._epoch:
			if self._chip.ready():
				self._chip.write(pending.pop(0))
			self._push_audio(self._chip.render(block), epoch)

	def _render_tail(self, epoch, seconds):
		# The rendered silence also flushes the stretcher's ~26 ms of
		# lookahead, so the utterance's true ending is always heard.
		block = max(1, int(self._chip.sample_rate * 0.012))
		for _ in range(int(seconds / 0.012) + 1):
			if epoch != self._epoch:
				return
			self._push_audio(self._chip.render(block), epoch)
