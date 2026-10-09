# How an unmodified MAME device runs outside MAME

`third_party/mame/votrax.cpp` is byte-identical to MAME's copy. That is
the point of the whole design: the definitive SC-01 simulation keeps its
provenance, MAME updates can be taken by copying two files, and nothing in
the vendored code ever needs review beyond "is it still identical".

(Since 2026-10-08 the build compiles `src/chip/votrax.cpp`, a copy that
differs only in lines marked `votraxsc01:`: three corrections (closure
timing, the stop-release thump and the noise balance), each switchable,
documented in closure-study.md. The shim serves both
copies alike, and taking a MAME update means re-copying `third_party/mame`
and re-applying the marked lines.)

The price is paid in `src/shim/emu.h`, and it is small — about 380 lines —
because the shim was **measured, not designed**: before writing it, the
vendored files were grepped for every MAME symbol they touch. The full
list: the `device_t`/`device_sound_interface` lifecycle, one
`sound_stream`, one `emu_timer` plus `attotime`, `required_memory_region`,
`devcb_write_line`, the `ROM_START` macro family, `bitswap`, `save_item`,
`line_state`, and the fixed-width aliases. Nothing else is implemented.

## The one real idea: time is driven by rendering

Real MAME has a scheduler: timers fire between stream updates, and devices
call `m_stream->update()` to render "everything up to now" before touching
state. Outside MAME there is no scheduler, so the relationship is
inverted:

- The host calls `shim_render(buffer, n)`. That call — and nothing else —
  advances emulated time, sample by sample.
- The render loop checks the device's timer before each stretch of
  samples and fires it at its exact position inside the block. The SC-01
  uses its timer for phone commit (~0.1 ms after a strobe) and for the
  end-of-phone A/R transition, so firing position is audible correctness,
  not pedantry.
- Every `m_stream->update()` in the vendored code becomes a no-op, which
  is *correct*, not a lie: control writes only happen between render
  calls, when the stream is always caught up.

The contract that makes this sound: one chip, one thread at a time.
Control writes and rendering must be interleaved on the same thread or
serialized by the caller — which every consumer in this repository does
(the NVDA driver funnels everything through its speak thread; the SAPI
engine serializes `Speak()` with a mutex; `say01` is single-threaded).

## Details that would bite a re-implementation

- **`bitswap` order**: the first listed bit position becomes the *most*
  significant bit of the result. The phone ROM unpacking depends on it.
- **`adjust()` defaults param to 0.** `device_clock_changed()` re-arms
  the timer without passing a param, which resets it to `T_COMMIT_PHONE`
  — faithful to MAME's behavior, so the shim keeps the same default.
- **`LOGMASKED` must not evaluate its arguments.** The one
  `machine().time()` call in the device lives inside a log statement;
  swallowing arguments unevaluated is what lets the shim have no
  `machine()` at all.
- **ROM byte order**: `ROM_REGION64_LE` + a plain byte load means the
  512-byte file maps straight into memory on a little-endian host; the
  device reads it as 64 little-endian `u64` entries. A plain `memcpy` is
  exactly right on x86.
- **`attotime` as double seconds** is exact enough here forever: at the
  chip's 720 kHz, resolution degrades below one clock tick only after
  centuries of continuous speech.
