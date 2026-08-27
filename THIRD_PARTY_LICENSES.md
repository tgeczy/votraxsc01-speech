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

The whole of this repository is BSD-3-Clause in MAME's honor; see LICENSE.

(Further third-party entries — e.g. the text-to-phoneme lineage — are added
here as they are vendored, each with its provenance and verbatim license.)
