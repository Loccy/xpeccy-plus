# Under the Hood

```{index} Under the Hood, how it works
```

The rest of this manual says what each part of Xpeccy+ does and where to find it. These pages
say how some of it works, and why it was done that way: the details that are not obvious from
the options, the problems behind them and how they were solved. Each page links to the settings
it explains. The section grows as more is written.

- [Low input lag](input-lag.md) - where the delay between a key and the screen comes from, and
  how to get the least of it.
- [Anti-flicker](antiflicker.md) - why Spectrum pictures flicker on a modern screen, and how
  the frames are mixed back without blurring what moves.
- [Tape loading by itself](tape-loading.md) - how a loader is recognized, and what auto play,
  fast loading, flash loading and edge detection do with it.
- [Keeping time and going back](frame-timing.md) - how the machine keeps its exact speed, what
  happens to the sound when time jumps, and how rewind keeps its history.
- [Contention, snow and the floating bus](ula-snow-floating-bus.md) - the processor and the
  picture sharing memory, and the three effects that come out of it.
- [A folder as a disk](folder-as-disk.md) - how a folder becomes a FAT32 disk, and how what the
  machine writes gets back into it.
- [What a quick save keeps](quick-saves-inside.md) - how the running game is found, where its
  saves go and what they carry.
- [SZX snapshots](szx.md) - what Xpeccy+ keeps in an `.szx` file, field by field, for the
  authors of other programs.

```{toctree}
:maxdepth: 2

input-lag
antiflicker
tape-loading
frame-timing
ula-snow-floating-bus
folder-as-disk
quick-saves-inside
szx
```
