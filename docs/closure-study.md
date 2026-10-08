# The closure study: why K sounded like A

*2026-10-08. One change to MAME's SC-01 model, measured against recordings of
real SC-01-A hardware. Code: `src/chip/votrax.cpp` (marked copy of the untouched
`third_party/mame/votrax.cpp`); switch: `vx_closure_fix()`; tests:
`tests/chip_test.py`.*

## The symptom

Spelled "K" came out as "A", and "cancel" as "an-sell". The stops P, T and K
were silent gaps with no release burst. The ROM does program noise into all
three (P fa=6, T fa=15, K fa=4, the same in both masks), and the data sheet's
Table 2 files them as *fricative stops*. A stop that is muted for its whole
length throws that noise away.

## What upstream MAME does

`chip_update()` latches the closure when the **current** phone's tick count
equals its `cld` field. `analog_calc()` applies the closure gain after F4, to
voice and noise together. So a stop's closure lasts until the *next* phone
reaches its own `cld` tick. Before a vowel that is tens of milliseconds, and
the stop's noise, which starts decaying at the vowel's `vd` tick, has died out
by the time the closure lifts. Rendered "K AY" is 93 ms of silence (peak 30
out of 32767) followed by an ordinary "AY". Before a short pause (PA0) the
closure lifts early, and a burst does come out. That proved the burst
mechanism exists; it was being masked by the closure timing.

## What the patent says

US 4,433,210 (Ostrowski/White, Federal Screw Works, the SC-01's own patent;
MAME already cites it for the switched-capacitor filters). Quoted from the
issued text:

- The closure delay "serves a similar function and is adapted to delay the
  transmission of the fricative amplitude", i.e. it is a delay of "the closure
  (CL) and fricative amplitude (FA) control parameters".
- The vocal delay "thus serves to delay the transmission of the vocal amplitude
  (VA) control signal".
- The closure release "typically will occur at the beginning of the following
  phoneme period if the following phoneme does not also require the closure
  function".

So closure and fricative amplitude share one delay. Upstream gates FA with the
field it calls `vd` and the closure with the field it calls `cld`, so the two
are on different delays.

## The change

The closure latches on the same ROM field that gates the noise amplitude:

```diff
-			if(m_ticks == m_rom_cld)
+			if(m_ticks == (m_closure_fix ? m_rom_vd : m_rom_cld))
 				m_cur_closure = m_rom_closure;
```

One reading is that the two 4-bit fields' names are swapped upstream and the
closure comparator is the one consumer that reads the wrong one. That reading
can't be confirmed without tracing the die, so it is only noted here. The
claim this study does make: putting closure and noise on one delay, as the
patent describes, matches the hardware recordings, and upstream does not.

## Evidence

**Recordings** (YouTube rips, kept local and never committed): a direct
line-in of an SC-01-A saying "Votrax SC-01A speech synthesizer" at four master
clocks (the README in the clips folder shows that pitch × phrase length is
constant within 1%). Also used: a microphone recording of a VIC-20 program,
and a Type 'N Talk capture.

**An aligned stream.** "speech" is S P E1 Y T CH in Votrax's own *Phonetic
Speech Dictionary for the SC-01* (1981), so the model renders the same phones
the recording most likely used. Each piece is measured by the same 1 ms
level-run method on both sides (−26 dB re peak, 4.5 kHz low-pass, so both have
the same bandwidth).

**Hold-out protocol.** Takes 1 and 4 were set aside before any tuning, and
predictions were written down before either was opened. Take 4 was predicted
first by scaling take 2 by the take-length ratio, then re-checked with a direct
render at the take's clock (shown below). Take 1's numbers come from direct
820 kHz renders.

| "speech", ms | S | P closure | T closure | CH |
|---|---|---|---|---|
| take 2 (720 kHz): real | ~80 | ~90 | ~70 | ~120 |
| fix | 75 | 85 | 70 | 115 |
| upstream | 55 | 130 | 90 | 115 |
| take 4 (520.6 kHz), hold-out: real | 119 | 114 | 92 | ~155 |
| fix (direct render) | 106 | 126 | 107 | 158 |
| upstream (direct render) | 77 | 191 | 140 | 158 |
| take 1 (820 kHz), hold-out: real | ~78 | ~70 | ~60 | ~105 |
| fix (direct render) | 66 | 78 | 66 | 99 |
| upstream (direct render) | 48 | 118 | 87 | 99 |

How each row was measured: the take 2 rows and take 1's real T closure and CH
were read from spectrograms and a 2 ms segmentation. Every other number comes
from the 1 ms run method on both sides.

The fix lands within 1–15 ms of the hardware at all three clocks. Upstream is
off by 25–77 ms, always in the same direction: S too short, closures too long.

**Bursts.** On the line-in, the T in "Votrax" peaks about 3 ms after release
and is strong for about 10 ms. The fix gives a burst of about 17 ms; upstream
gives essentially none. On the microphone recording, "neck" and "bitten" show
short broadband bursts at their stop releases. In upstream's render of the
same words they are absent.

**Listening.** The project's blind maintainer judged the fix better on
"category", "capacity" and "neck … bitten", and judged it no worse on "next".

## Variants tried and rejected

| Variant | Result |
|---|---|
| Closure gates the voice path only | 100–170 ms of hiss on P/T; the hardware's bursts are ~10–30 ms. Rejected. |
| Release at the next phone's commit | Burst present, but a ~30 ms full-level plateau ("sharp, clipped T" by ear). |
| Release at tick 1 of the next phone | Same plateau, slightly shorter. Preferred over the above by ear, but beaten by the fix. |

## Remaining differences (not fixed)

- The fix's bursts are 16–22 ms against the hardware's 10–12 ms.
- In all three takes, S is about 12 ms short and closures 7–15 ms long.
- The hardware has a sharp transient where an S runs into a closure. The model
  fades out over 1.4 ms instead.
- K's burst is real but quiet: 17 ms at −17.5 dB relative to the vowel (ROM
  fa=4). Whether the hardware's initial K is as weak is unknown; no aligned
  recording of one exists yet.
- An S straight after a K is shortened (104 → 84 ms without a pause). Votrax's
  dictionary always writes K PA0 S (access, next, six, syntax), which suggests
  the real chip needed the same workaround.

## Reproducing

`vx_closure_fix(chip, 0)` renders upstream MAME bit for bit. `chip_test.py`
pins this with a SHA-256 fingerprint taken from the unmodified file, and
checks that K and P get a release burst of at least 20 ms with the fix and at
most 3 ms without it.

Sources: US 4,433,210 (patents.google.com/patent/US4433210A); Votrax SC-01
data sheet (1980/1983 scans); Votrax *Phonetic Speech Dictionary for the SC-01*
(1981). Codex's investigation report (patent lineage, and MAME history up to
3314e1bf, which is what `third_party/mame` holds) informed the hypothesis.
