# Built-in ROM labels

Names for the routines of known ROMs, the ranges in them that are data, and the system
variables of their systems, shown by the Debugger where the user has no label or mark of
their own. Switch them off in Options - Debugger - **ROM labels** and **System variables**.
The lists are the `.labels` files in this folder, read by the program when it starts; none
is built into it. This file says where they come from.

A ROM is known by its bytes: each list names the MD5 of every 16K bank it fits, so a ROM file
with the same bytes gets the names wherever it comes from, and a changed one does not. A list
also fits the patched copies of its ROM in the clones (the 48 BASIC of the 128K, +2, +3,
Pentagon, Scorpion, Profi, ATM and ZX Evo; the 128 editor of the Pentagon, Scorpion and the
others), whose code sits at the same places; a name or a data range whose bytes were changed on
such a ROM is left out there. The +2 ROM 0 has a list of its own: the 128K ROM 0's names moved
to where the +2 has the same code.

Every name is in the namespace of its system - `BASIC48`, `BASIC128`, `P3ROM0`, `P3ROM1`,
`P3DOS`, `TRDOS` - as `BASIC48.LD_BYTES`; the short name works too where no other system in
the machine and no label of the user's has it.

Like the ROM images, **these lists are not all covered by the MIT license of Xpeccy+** - each
one keeps the terms of its own source, listed below.

## The format

```
@ns BASIC128                      namespace of the names below
@rom <MD5> <image> [-NAME] [-@ADDR]   a 16K bank the list fits; what it has not
@ram 5                            the names below are RAM cells of bank 5 (system variables)
0556 LD_BYTES                     a name: address, name
3D00 3FFF db                      data: first, last address, view (db, dw, da, text)
```

`text` ends at a character with bit 7 set when there is one, shown as `DC "..."`.

## 48K BASIC - SkoolKit, GNU GPL v3 or later

`48.labels` in this folder: 1099 names and 142 data ranges from SkoolKit's
`examples/48.rom.ctl` (commit 30de485a, 2015, <https://github.com/skoolkid/skoolkit>), Copyright
2011-2015 Richard Dymond, part of SkoolKit and so under the **GNU General Public License,
version 3 or later** - the text is in `GPL-3.0.txt` here. Its routine titles are "taken from
'The Complete Spectrum ROM Disassembly' by Dr Ian Logan and Dr Frank O'Hara, published by
Melbourne House". It is a separate data file, read by the program at run time and not built
into it, so the program itself stays under the MIT license. Changed for Xpeccy+ on
2026-11-07: `C_ARC_GE1` moved from #2347 to #235A, where the ROM's own `JR NC` at #2350 goes;
`SV_SLICE?` renamed `SV_SLICE_Q` and `val` renamed `val_fn`, since a name in the Debugger's
expressions cannot hold a `?` and `VAL` is one of their pseudo-variables. 2026-11-08: the data
ranges of its `b`, `t` and `w` blocks and `B`, `T` and `W` sub-blocks added.

## 128K, +2A/+3 - Klive IDE, MIT

`basic128.labels` (128K ROM 0, 1085 names), `basic128-plus2.labels` (+2 ROM 0, 757 of
them carried over), `p3rom0-40/41`, `p3rom1-40/41` (+2A/+3 ROM 0 and ROM 1, v4.0 and v4.1),
`p3dos-40/41` (ROM 2, with the jump table below), and `basic48-sp128-1`, `basic48-spp3-40-3`,
`basic48-spp3-41-3` (the 48 BASIC of these machines where it differs from the 48K's). Names and
data ranges from the ROM annotations of Klive IDE by Istvan Novak
(<https://github.com/Dotneteer/kliveide>, `src/public/roms`, commit e227b87c, 2026-11-06),
written from the ROM bytes and the manuals, under the **MIT license**:

```
MIT License

Copyright (c) 2021 草鞋没号

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

Added for Xpeccy+: the bytes after `RST #28` (an address in the other ROM) and after a call of
the error routine (a report code) marked as data, where the ROM's own code reads them.

## +3DOS jump table - Xpeccy+

In `p3dos-40/41.labels`: the jump table at #0100 of the +2A/+3 ROM 2, 53 entries, under the
names the +3 manual gives the +3DOS and disk driver calls (`DOS_OPEN`, `DD_READ_SECTOR`...).
Written for this project, MIT license.

## TR-DOS - Xpeccy+

`trdos.labels`: the entry points the TR-DOS manuals document - #3D00
(`TRDOS_ENTRY`, from BASIC), #3D03 (`TRDOS_REM`, a command in a REM), #3D13
(`TRDOS_FUNCTION`, the function in C), #3D2F (`TRDOS_RET`, a RET that jumps to the address
pushed). 5.03, 5.04T and the TR-DOS pages of the bundled clone ROMs. Written for this
project, MIT license.

`trdos-503.labels`: 48 names read from TR-DOS 5.03's own tables - the functions #3D13 calls by
the number in C (`FN_READ_SECTORS`, `FN_FIND_FILE`..., the table at #288C), named after what
each does in the code and in the TR-DOS manuals, and the handlers of its BASIC commands
(`CMD_CAT`, `CMD_FORMAT`..., the tokens at #2FF3 and their addresses at #3008). Also on 5.04T
and the clones' TR-DOS pages, where the code is the same. Written for this project, MIT
license.

## System variables - Xpeccy+

Shown in operands and tips when the system's ROM is in the machine, written for this
project, MIT license: `sysvars-basic48.labels` (#5C00-#5CB5, the names of the ZX Spectrum BASIC
manual, chapter 25), `sysvars-basic128.labels` (#5B00-#5BFF, the ZX Spectrum +2 manual, chapter
8 part 25), `sysvars-p3rom0.labels` (#5B00-#5BFF of the +2A/+3, the +3 manual, chapter 8 part
25), `sysvars-trdos.labels` (#5CC2-#5D26, names given by Xpeccy+ to the variables described in
"Tr-Dos v5.xx variables in Beta 128 Disk interface", Mac Buster^Extreme, 1999-2000).
