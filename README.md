# votraxsc01-nvda

The **Votrax SC-01** — the 1980 phoneme chip behind the Type 'N Talk, the
Apple II Mockingboard's speech, the HERO-1 robot and a generation of
arcade machines — as a speech synthesizer for today's Windows: an **NVDA
add-on** and a **SAPI 5 voice**, from one shared core.

The speech is not imitated. The core is MAME's silicon-level simulation
of the chip (Olivier Galibert's `votrax.cpp`, BSD-3-Clause), its filters
derived from the patent schematics and its behavior from the decapped die,
vendored here **unmodified** and compiled with one marked, switchable
correction backed by recordings of real chips (see below). Both mask revisions are supported and appear
as separate voices — SC-01 and SC-01-A genuinely sound different, and
neither replaces the other.

## What's in the box

| Piece | Where | What it is |
|---|---|---|
| The chip | `src/chip/` + `src/shim/` | MAME's device (upstream kept byte-identical in `third_party/mame/`), compiled outside MAME by a ~380-line `emu.h` stand-in, plus one marked closure-timing fix that restores P/T/K bursts (see [docs/closure-study.md](docs/closure-study.md)) |
| C core | `src/core/sc01.h` | The flat API everything shares: create (CRC-verified ROM), write phoneme, poll ready, render samples |
| Text-to-phoneme | `src/frontend/` | English text → SC-01 phoneme codes, one C implementation for all consumers |
| Probe | `tools/say01.c` | Command line: phoneme strings or the whole 64-phone table → WAV; the reference harness |
| NVDA add-on | `nvda-addon/` | Native 64-bit driver (with a 32-bit DLL for older NVDA); constant-pitch rate by phone truncation, with an "authentic rate" option that is the master clock, exactly like the hardware |
| SAPI 5 voice | `sapi/` | Single-file engine; JAWS word-bookmark batching, abort/skip polling and the other conventions of a tried and tested engine |
| Installer | `installer/` | Inno Setup: both bitnesses registered, ROMs picked up from beside the setup file |

## ROMs

The chip's internal 512-byte mask ROM is required and **not included**
(its copyright is uncleared; Votrax is long gone). See `roms/README.md`
for the file names and CRCs — they are the standard MAME dumps. Every
loader verifies the CRC and names the expected value when it refuses.

## Building

MSVC Build Tools 2022 + a Windows SDK, then:

```powershell
.\build.ps1              # probe + core DLLs + SAPI DLLs + NVDA add-on
ISCC installer\votraxsc01.iss   # the installer, afterwards
```

Everything is built `/MT` (static CRT) and the build fails if a binary
picks up a VC++ runtime dependency — the add-on must work on machines
that never installed a redistributable.

First sound without installing anything:

```powershell
.\build\say01.exe --rom roms\sc01a.bin --phones "H EH1 L OO1"
.\build\say01.exe --rom roms\sc01a.bin --table   # all 64 phones, indexed
```

## Known limitations

This is a faithful reproduction of a 1980 phoneme chip, not a modern
clear synthesizer, and some of what you hear is the chip (or the model of
it), not the driver:

- **Stops are still weak.** Upstream MAME's model kept the closure on too
  long, so P, T and K lost the release burst the ROM programs for them
  ("K" sounded like "A"). This build fixes the closure timing, following
  Votrax's own patent and measured against line-in recordings of a real
  SC-01-A at three clocks ([docs/closure-study.md](docs/closure-study.md)).
  The bursts are back but short and, for K, quiet: Votrax gave K the least
  noise of the three. B, D and G carry no burst in the ROM at all, so "D"
  next to "E" is the chip itself.
- **Words are pronounced by 1976 rules.** The text-to-phoneme stage is the
  NRL letter-to-sound rules of the Votrax era, so many words are wrong the
  way they were wrong then ("select" → "sealect"). A measured exception
  dictionary fixes the worst offenders; a full pronunciation dictionary
  (CMUDict-style) would be the real remedy and is a possible future
  addition. Letter names the rules got wrong (o, u) are corrected.
- **The voice is not very intelligible**, especially at first. That is the
  SC-01. The SSI-263 was the clearer chip; this is deliberately the older,
  rougher one, preserved as it was.

## Design notes

- `docs/shim-design.md` — how an unmodified MAME device runs outside MAME
- `docs/closure-study.md` — the stop-consonant fix, its patent basis and the
  measurements
- `docs/history.md` — the chip's story and sources

## Credits

- **Olivier Galibert** and the MAME project — the chip simulation this
  exists to carry further. The whole repository is BSD-3-Clause in kind.
- The Speak-loop conventions follow the author's TGSpeechBox SAPI engine;
  the COM scaffold, panthera-speech; the add-on shape, Jayson Smith's
  EchoTalk.
- Text-to-phoneme lineage: NRL Report 7948 (Elovitz, Johnson, McHugh &
  Shore, 1976), the letter-to-sound rules of the Votrax era.
