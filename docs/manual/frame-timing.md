# Keeping time and going back

```{index} timing; how it works, frame rate; exact, pacing, Rewind; how it works
```

A ZX Spectrum does not run at a round speed: a 48K makes 50.08 frames a second, a Pentagon
48.83. To sound and move like the real machine for hours, an emulator has to keep that rate
exactly, against a computer whose own clocks run at other speeds. This page says how Xpeccy+
keeps time, what happens to the sound when the time jumps, and how
[Rewind](time.md#rewind) keeps its history.

## Time to a fraction of a nanosecond

The machine's time is counted in steps of its clock: the dot the picture is drawn by, the
processor's tick, the sound's sample. At 3.5 MHz a tick is 285.714... ns - not a whole number.
Rounded to 286 ns it would be 0.1% too long: a 48K would make 50.03 frames a second instead of
50.08, a frame fewer every 20 seconds, and its music would be that much flat.

So these periods are kept in fixed point, to 1/65536 of a nanosecond, and what one step leaves
over is carried into the next instead of being dropped. The periods are also made from one
another, not rounded each on its own: a tick is exactly two dots, and a frame exactly so many
of them. Two periods rounded apart would drift against each other - a picture standing still
would creep by a dot every few seconds.

| Machine | Frame | Frames a second |
|---|---|---|
| ZX Spectrum 48K and its kind | 69888 ticks at 3.5 MHz | 50.08 |
| ZX Spectrum 128K, +2, +2A, +3 | 70908 ticks at 3.5469 MHz | 50.02 |
| Pentagon, ZX Evolution | 71680 ticks at 3.5 MHz | 48.83 |

## What sets the pace

The machine is paced by the computer's clock: every 2 ms the emulation is given the time that
has passed, as so many sound samples to make, and runs until it has made them. The sound card's
own clock is not used for it, as the card asks for sound only every 10 to 20 ms, too coarsely
to pace frames by.

The two clocks - the computer's and the sound card's - never quite agree. Left alone, the sound
waiting to be played would slowly run out (a click) or pile up (more delay). So the pace is
steered by up to 0.1% to keep the waiting sound at its target: far too little to hear, or to
see in the frame rate. With [VSync](options-video.md#vsync) locking the machine to the screen,
the screen's refreshes set the pace instead, and the sound is steered by up to 0.5%.

After a stall - a window dragged, a disk slow to answer - the machine makes up at most 100 ms;
it does not race to catch up on more.

**Slow Motion** and **Fast Forward** do not change the pace; they change how much of the
machine's time each sample stands for. The sound card still gets every sample it asks for, and
the sound goes down or up in pitch, as a tape played slow or fast.

## When the time jumps

The sound waiting to be played follows on from the machine's last moment. After Fast Mode, a
rewind, a file opened, a reset or another machine, the machine's sound no longer follows on
from it. That sound is dropped, the program waits until enough new sound is ready to play
without running short, and fades from the last sample played into the new sound over 5 ms, so
there is no click. A pause drops nothing: what waits is what comes next.

The frame rate in the status bar is counted over the last second, from one frame's end to
another's, so it has no rounding of its own.

## Rewind

```{index} Rewind; history, Rewind; memory
```

**Taking the history.** Every **Step** frames (3 by default), the whole machine is copied in
memory - the same copy [Run ahead](input-lag.md#run-ahead) uses, not a file. A copy of a
Pentagon 1024 or a ZX Evolution is megabytes, so each copy is cut into pages of 4 KB, and a page
that is the same as in the copy before is shared, not copied again. A few frames apart, most of
memory is the same. The sound is kept beside the copies, in the machine's own time, so a
stretch played fast or slow keeps its sound in step with its pictures. With the defaults the
history takes 20 to 45 MB, and never more than 512 MB, whatever **History** asks.

**Going back.** Each picture shown going back is the next copy back. It is not stored as a
picture: the copy is loaded and one frame is run from it and thrown away, which makes the exact
picture, multicolor effects included, for the price of one frame. So going back runs **Step**
times faster than the machine did: 30 seconds of history in about 10 at the default step. The
sound plays each stretch backwards.

**Letting go.** The machine is put back on the copy on the screen, and everything newer is
dropped. Cheats are put back as they were at that moment.

**What is not in the copy.** The keys and the joystick, so a key held now stays held. The
contents of disk, hard disk and card images: they are too big to copy every few frames. That is
why the history starts over when the machine writes to one - nothing before the write could be
put back as it was. It also starts over with another machine, with an RZX recording starting or
ending, and when **Apply** in Options changes the machine or the rewind's own settings. A reset
or a file opened on the same machine keeps it, so an accidental reset can be undone.
