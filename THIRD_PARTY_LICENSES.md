# Third-party code

## MAME Votrax SC-01/SC-01A simulation

`third_party/mame/votrax.cpp` and `third_party/mame/votrax.h` are taken
**unmodified** from the MAME project (https://github.com/mamedev/mame,
`src/devices/sound/votrax.cpp` at the state vendored here), written by
**Olivier Galibert** and licensed BSD-3-Clause, as their headers state:

```
// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
```

This simulation is the definitive model of the chip: its filters are
derived from the patent schematics and its behavior from the decapped die.
Keeping the files byte-identical to MAME's is a design goal — all
adaptation lives in `src/shim/emu.h`, never in the vendored code — so that
future MAME improvements can be picked up by copying the two files again.

**What is compiled is a modified copy.** `src/chip/votrax.cpp` and
`src/chip/votrax.h` copy the vendored files with Galibert's license and
copyright headers intact. They differ only in lines marked `votraxsc01:`,
which carry one closure-timing fix that can be switched off
(`vx_closure_fix()`; off renders upstream bit for bit, which a test checks).
The evidence for the fix is in docs/closure-study.md.
`third_party/mame/` stays upstream's file, as the reference and the base for
future merges.

The whole of this repository is BSD-3-Clause in MAME's honor; see LICENSE.

## Wasser English-to-phoneme translation (the NRL rules in C)

`third_party/wasser/english.c`, `phoneme.c`, `saynum.c`, `spellword.c`
(plus the `AUTHOR` and `BUGS` postings, and `parse.c.orig` for reference)
are **John A. Wasser's** "English to Phoneme translation", posted to
net.sources on 15 April 1985 (Message-ID `<1679@decwrl.UUCP>`, "Final
English-to-Phoneme version!"), vendored **byte-identical** from the
USENIX 1987 tape (github.com/sergev/Usenix_Tapes,
`usenix87/Utilities/Phoneme/`). It implements the letter-to-sound rules
of NRL Report 7948.

License, verbatim from the author's own posting:

> If you make a major addition (like better abbreviation handling or an
> exception dictionary) please send me a copy.  As before, this is all
> public domain and I make no copyright claims on it.  The part derived
> from the Naval Research Lab should be public anyway.  Sell it if you
> can!  -John A. Wasser

Only `parse.c` (file I/O and `main()`) is not compiled as-is: it is
replaced by `src/frontend/wasser_parse.c`, a string-driven adaptation
that preserves its word-assembly logic and is public domain following its
source. The CMU AI Repository catalog independently records this package
as "Copying: Public Domain".

## NRL Report 7948 (the rules themselves)

The letter-to-sound rules and the IPA-to-Votrax translation rules
transliterated in `src/frontend/arpabet_to_sc01.c` come from *Automatic
Translation of English Text to Phonetics by Means of Letter-to-Sound
Rules* (Elovitz, Johnson, McHugh & Shore, NRL Report 7948, 1976,
AD/A021 929) — a work of the United States Government, public domain
under 17 USC 105.
