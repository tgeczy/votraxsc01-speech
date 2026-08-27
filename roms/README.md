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
