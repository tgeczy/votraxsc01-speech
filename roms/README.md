# ROMs — the chip's internal mask ROM

Nothing in this repository makes sound without the Votrax SC-01's 512-byte
internal phone ROM, dumped from decapped silicon by the MAME project.
Copyright in these dumps is uncleared (Votrax ceased to exist decades ago),
so they are **not** included here. Place one or both files in this folder:

| File | Chip | Size | CRC32 |
|---|---|---|---|
| `sc01.bin` | SC-01 (earlier mask) | 512 bytes | `528d1c57` |
| `sc01a.bin` | SC-01-A (later mask) | 512 bytes | `fc416227` |

These are the same files MAME expects, found inside any machine set that
contains the chip (`votrtnt`, `votrpss`, `gorf`, `qbert`, …). Every loader
in this project verifies the CRC before starting the chip and names the
expected value when it refuses, so a wrong file can never produce silently
wrong audio.

**Distribution policy** (the same one this author's PC-ROBOT and BraiLab
preservation work follows): this *repository* never carries the dumps —
they are not our work, and their copyright, while almost certainly
ownerless after four decades, is uncleared. *Release* bundles built on a
machine that has the files here in `roms/` do include them, carrying
MAME's licensing and provenance notes, so end users get voices that speak
out of the box. Being careful and being useful are both possible.

## Why these 512 bytes are an orphan work

The status was researched before the first release; the short version:

- **Provenance of the dumps**: read optically from decapped dies by the
  MAME project (Olivier Galibert and Lord Nightmare), required by MAME
  for Q\*bert, Gorf and Wizard of Wor sets for well over a decade, and
  publicly archived that whole time without a takedown.
- **Patents**: Gagnon's (1974, 1975) and Dorais & Ostrowski's (1978,
  assigned to Federal Screw Works) expired decades ago.
- **Mask-work protection**: the Semiconductor Chip Protection Act
  post-dates the chip (1984) and its ten-year term is long over.
- **Copyright, if any**, covers ~half a kilobyte of phoneme parameter
  tables — much nearer the "data, not expression" end of the spectrum
  than a program.
- **The ownership trail** dissolves: Votrax began as the Vocal division
  of Federal Screw Works, split off in 1980 as Votrax International,
  left speech in 1984 amid restructuring, merged into Vynet (1987),
  became Maxxar (1995), which was acquired by Open Solutions (2004),
  acquired in turn by Fiserv (2013); the Votrax trademark lapsed in
  2016. Federal Screw Works itself still makes fasteners in Romulus,
  Michigan. Whatever rights exist sit unclaimed between a bolt
  manufacturer and a fintech's acquisition ledger, with a 1984
  restructuring muddying which of them ever received it.

Nobody in that chain has shown any awareness of the chip in forty
years. If a rights-holder ever does surface, contact the maintainer and
the dumps will be removed from future releases.
