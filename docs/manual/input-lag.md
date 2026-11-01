# Low input lag

```{index} input lag; how it works, latency, Run ahead; how it works, Low latency; how it works
```

Input lag is the time from pressing a key to seeing what it did. On a real Spectrum it is
short: the program reads the keyboard once a frame and draws the result in the same frame or
the next one, and the television shows it as the beam passes. An emulator adds steps of its
own between the key and the screen. This page says where each one comes from and what
Xpeccy+ does about it. The settings are on the
[Emulation page](options-emulation.md#input-lag) and the [Video page](options-video.md#vsync).

At 50 Hz a frame is 20 ms, so each frame saved below is 20 ms.

## Where lag comes from

- **The machine itself.** A program usually reads the keys once a frame, often right after
  the frame interrupt, and draws what follows over the rest of the frame. A key pressed just
  after the read waits for the next one. This frame is the hardware's own; a real Spectrum
  has it too.
- **Waiting to be shown.** An emulator makes a frame whole and then hands it over to be shown.
  If finished frames wait in a line, each waits behind the ones before it.
- **The screen's own rate.** The machine makes 50 frames a second, the screen refreshes 60,
  100 or 144 times a second. A finished frame waits for the next refresh.
- **The desktop.** A window is drawn by the system's compositor, which puts it on the screen
  one refresh later.
- **The monitor or television.** Some of them process the picture before showing it; a
  "game mode" turns that off. Nothing in a program can change it.

The keys themselves add almost nothing: a key press reaches the machine as soon as the
system reports it, not once a frame, and gamepads are read every 2 ms.

## What Xpeccy+ does about it

```{index} Low latency; frames shown, fullscreen; compositor
```

**The newest frame, at once.** With **Low latency** on, each frame is shown as soon as it is
made; a frame that was not shown yet when a newer one is ready is dropped. With it off, up to
two frames wait in line: motion is more even on a screen whose rate does not match the
machine's, but each frame is shown up to two frames later. It is on by default.

**Fullscreen past the compositor (Windows).** In fullscreen with **Low latency** on and
**VSync** off, the picture goes to the screen without the compositor, which saves its frame.
A menu or a dialog has to be composed with the picture, so while one is open the picture goes
through the compositor again; the switch is the short flicker seen when a menu opens. In a
window the compositor is always there.

**VSync, late in the refresh.** With **VSync** on, the picture waits for the screen's
refresh, so it never tears, and a machine close to the screen's rate runs at that rate (see
[VSync](options-video.md#vsync)). Waiting for a refresh could add lag of its own, so the next
frame is not started as soon as a refresh passes: it is started late enough to be finished
just before the next refresh. The machine reads its keys later, and the frame is still in
time. On Windows a fullscreen picture with **VSync** goes through the compositor, which is
what keeps it from tearing, and that costs the compositor's frame.

## Run ahead

```{index} Run ahead; cost, Run ahead; sound
```

The steps above take away what the emulator adds. **Run ahead** takes away part of the
machine's own lag, which no real Spectrum can do.

At the end of every frame, the whole machine - memory, processor and every chip - is copied
in memory. The machine then runs one or two frames further, that picture is shown, and the
copy is put back. The keys and the joystick are not put back with it, so a key pressed is
still pressed in the frames that follow. What a key press does is therefore shown one or two
frames earlier than the machine itself would show it.

What it costs:

- **Processor time.** Every frame is run at least twice: once for real, once ahead. With
  **2 frames**, the frames run ahead are kept while nothing reaches the machine, and only one
  new one is run each time; a key press makes both run again, three times the usual work for
  that frame.
- **Sound.** The sound comes from the frames the machine really runs, so it stays as many
  frames behind the picture as it runs ahead.
- **A frame at the switch.** When run ahead stops or starts again, one frame is shown twice
  or skipped.

A frame run ahead writes nothing to a disk, a card or a hard disk, and stops at no breakpoint:
it is thrown away, and the machine does all of it for real a frame or two later.

Run ahead stands down by itself whenever the copy cannot hold what happens next, or there is
nothing to gain:

- while a tape plays - the tape's signal is not in the copy;
- while the disk controller is busy with a command;
- while an RZX recording plays - its input is fixed, there are no keys to answer;
- in Fast Mode and Fast Forward, while the machine is paused or rewound, while the
  **Debugger** is open, and while the program types a file's start command.

## The least lag, step by step

1. Keep **Low latency** on.
2. Play in fullscreen. On Windows, with **VSync** off, this skips the compositor's frame.
3. On a screen with a variable refresh rate (G-Sync, FreeSync, Adaptive-Sync), leave
   **VSync** off: the screen shows each frame when it is ready, with no tearing and no wait.
   On any other screen, turn **VSync** on only if tearing bothers you.
4. Set **Run ahead** to **1 frame**. That takes away the machine's own frame for most
   programs; **2 frames** helps a program that reads the keys once in two frames.
5. Lower the sound's **Latency** on the [Sound page](options-sound.md) if the sound comes
   noticeably after the picture; it is how much sound waits ready to be played.
6. Turn on the monitor's game mode, if it has one.
