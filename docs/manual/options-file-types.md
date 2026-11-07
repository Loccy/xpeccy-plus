# File types page

```{index} File types page
```

![The File types page](images/options-filetypes.png)

What opening a tape or a disk does - run it, mount it, or ask - and whether the media in
the drives come back at the next start. The table says which machine each kind of file
opens on.

```{index} machine; switched by a file, Opened on, Run opened media, Mount opened media, Remember mounted media
```

What a file needs and which machine it switches to are explained in
[Run a program](getting-started.md#run-a-program). The **File types** page of **Options**
sets this for each format.

The **General** group at the top of the page:

- **Run opened media**, **Mount opened media** or **Ask: run or mount** - what opening a
  tape or a disk does. **Ask** shows the **Open Media** question; ticking
  **Remember my choice** there saves your answer in this setting.
- **Remember mounted media** - the tape and the disks in the drives are opened again at the
  next start.

The **File types** group has a row per format:

- **Format** - the kind of file.
- **Needs** - what a machine must have to run it: **any machine**, **Beta Disk (TR-DOS)**,
  **+3 disk drive**, **TSConf**, **ZX Spectrum Next**, **read from the file** (a `.z80` names its
  model, a `.sna` only 48K or 128K), **exactly the one it was taken on** (SZX) or **exactly the one it was recorded
  on** (RZX).
- **Opened on** - **Auto**, **Ask**, **Keep current**, or one machine from the list.

A tape runs on any machine, so its rows have no **Ask**: **Auto** in these rows only
switches back to your own machine after another file switched away from it. A machine
picked in **Opened on**
is used for every file of that format that it can run. **Restore defaults** sets every
row back to **Auto**.

Only a file that is about to run can switch the machine: a snapshot always, a tape or a disk
only when it is run, and a disk only from drive A.

```{index} snapshot; which machine runs it
```

A `.z80` or `.sna` snapshot runs on any machine with its memory paging. One taken inside
the ROM - in the BASIC editor, or with the program in the middle of a ROM routine - also
needs the very same ROM. The 128K and the clones (Pentagon, Scorpion and the others) share
one; the +2 has its own, and the +2A and +3 theirs, so such a snapshot from a 128K is
moved off them. A machine of your own counts by its hardware, whatever ROM files it has.
