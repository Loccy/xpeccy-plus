# What a quick save keeps

```{index} Quick saves; how it works, game; quick saves, Undo Quick Load; how it works
```

[Quick saves](files.md#quick-saves) are kept per game, so a game's saves stay together and
another game's do not push them out. For that the program has to know which game is running,
without looking inside the machine's DOS. This page says how it decides, where the saves go and
what they carry. The settings are on the [Saves page](options-saves.md).

## Which game is running

The game is the image last opened into drive A - a tape, a disk or a snapshot - or, since then,
the tape that starts playing or the disk in drive A whose motor starts, as when a program is
run from TR-DOS. Drives B to D and the cards never decide it: a disk in drive B is a save disk as
often as not, and a card is a whole system.

A game is known by its content, not by its file name: the same game opened as a bare file, from
an archive or from another folder shares one set of saves. An image over 16 MB is known by its
path instead, as reading gigabytes at every open would take too long.

The game ends at a reset you make - from the menu, a key, **Hard Reset**, the **Debugger** - and
at a change of machine; after that, saves go to the machine's own folder until a tape or drive A
starts something again. A program that jumps to address 0 by itself is not a reset you made,
and changes nothing. Loading a save brings its game back with it.

## Where the saves go

Each game has a folder of its own in the saves folder, named after the game with a few
characters of its fingerprint: `Exolon - 3f2a91c0`. Without a game, saves go into one per
machine. Every save is an [SZX snapshot](szx.md) with a picture beside it, and its name is the
time it was taken, so the newest is always known without a counter. When a kind of save - quick,
on exit, timed - reaches its number, the oldest of that kind goes.

## What a save carries

A save carries the tape, with the place it stood at, and the floppy disks as they are in memory
at that moment, changes not yet saved included. So a save loads even after the image was moved
or deleted, and the game finds on its disk what it wrote there. Hard disks and cards are not
carried: they are far too big, and are named only.

A save opens on the machine it was taken on, switching to it first.

## Undo

Before every quick load, the machine as it stands is kept in memory, with its media and its
game. **Undo Quick Load** puts it back; used again, it goes forward to the load. This copy lives
only in memory and is gone when the program closes.
