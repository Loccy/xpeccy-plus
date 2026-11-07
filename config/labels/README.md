# Built-in ROM labels

Names for the routines of known ROMs, shown by the Debugger when no label of the user's is at
that address. Switch them off in Options - Debugger - **ROM labels**. The lists of Xpeccy+ are
built into the program (`res/labels/` in the sources); a list under a license of its own is a
file in this folder, read by the program when it starts. This file says where they come from.

A ROM is known by its bytes: each list names the MD5 of every 16K bank it fits, so a ROM file
with the same bytes gets the names wherever it comes from, and a changed one does not. The 48K
list also fits the 48 BASIC of the 128K, +2, +3, Pentagon, Scorpion, Profi, ATM and ZX Evo,
whose code sits at the same places; a name whose code was patched on such a ROM is left out
there.

Like the ROM images, **these lists are not all covered by the MIT license of Xpeccy+** - each
one keeps the terms of its own source, listed below.

## 48K BASIC - SkoolKit, GNU GPL v3 or later

`48.labels` in this folder: 1099 names from SkoolKit's `examples/48.rom.ctl` (commit
30de485a, 2015, <https://github.com/skoolkid/skoolkit>), Copyright 2011-2015 Richard Dymond,
part of SkoolKit and so under the **GNU General Public License, version 3 or later** - the
text is in `GPL-3.0.txt` here. Its routine titles are "taken from 'The Complete Spectrum ROM
Disassembly' by Dr Ian Logan and Dr Frank O'Hara, published by Melbourne House". It is a
separate data file, read by the program at run time and not built into it, so the program
itself stays under the MIT license. Changed for Xpeccy+ on 2026-11-07: `C_ARC_GE1` moved
from #2347 to #235A, where the ROM's own `JR NC` at #2350 goes; `SV_SLICE?` renamed
`SV_SLICE_Q` and `val` renamed `val_fn`, since a name in the Debugger's expressions cannot
hold a `?` and `VAL` is one of their pseudo-variables.

## +3DOS - Xpeccy+

`plus3dos.labels`, built in: the jump table at #0100 of the +2A/+3 ROM 2 (v4.0 and v4.1), 53
entries, under the names the +3 manual gives the +3DOS and disk driver calls (`DOS_OPEN`,
`DD_READ_SECTOR`...). Written for this project, MIT license.

## TR-DOS - Xpeccy+

`trdos.labels`, built in: the entry points the TR-DOS manuals document - #3D00
(`TRDOS_ENTRY`, from BASIC), #3D03 (`TRDOS_REM`, a command in a REM), #3D13
(`TRDOS_FUNCTION`, the function in C), #3D2F (`TRDOS_RET`, a RET that jumps to the address
pushed). 5.03, 5.04T and the TR-DOS pages of the bundled clone ROMs. Written for this
project, MIT license.
