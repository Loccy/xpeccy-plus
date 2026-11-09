# DeZog (VS Code)

```{index} DeZog, VS Code, debugging; from VS Code, DZRP
```

[DeZog](https://github.com/maziac/DeZog) is a debugger for Z80 programs that runs in
Visual Studio Code. With it you debug your program from its source: you set breakpoints
on the lines of your assembler file, step through them, and see the registers, the memory
and the call stack in VS Code, while the program runs in Xpeccy+.

DeZog talks to Xpeccy+ over a network connection on this computer. Xpeccy+ speaks DeZog's
own protocol (DZRP), so no plugin or bridge is needed.

## Turning it on

```{index} DeZog; port, --dezog, --dezog-port
```

- **Options > Application > DeZog port** - tick it and set the port, 14000 by default. The
  setting stays.
- `--dezog` on the command line - on for this run, on the port Options set.
  `--dezog-port N` - on for this run, on port N. See [Command line](command-line.md#debugging).

Xpeccy+ then waits for DeZog on that port. Only programs on this computer can connect, and
only one at a time. While the option is off, nothing listens.

## A session

```{index} DeZog; launch.json, DeZog; session
```

1. Assemble your program with SjASMPlus and its `--sld` option, which writes the file
   DeZog maps the addresses to your source lines with.
2. Open the program in Xpeccy+: on the command line (`xpeccy-plus game.sna --dezog`), from
   the menu, or by dropping it on the window. A tape or a disk works too.
3. In VS Code, start debugging with a `launch.json` like the one below.

DeZog connects, and the machine stops where it was. Press **Continue** in VS Code to run
it. When DeZog closes the connection, its breakpoints go and the machine runs on.

For DeZog 3.8 and later:

```json
{
	"version": "0.2.0",
	"configurations": [
		{
			"type": "dezog",
			"request": "launch",
			"name": "Xpeccy+",
			"remoteType": "dzrp",
			"dzrp": {"port": 14000},
			"sjasmplus": [{"path": "main.sld"}],
			"topOfStack": "stack_top",
			"rootFolder": "${workspaceFolder}",
			"startAutomatically": false
		}
	]
}
```

DeZog 3.7 has no remote type of its own for an emulator yet; use the one it names `cspect`,
with the same port:

```json
			"remoteType": "cspect",
			"cspect": {"hostname": "localhost", "port": 14000},
```

DeZog can load the program itself, with `"load": "game.sna"` (a `.sna`, `.z80` or `.nex`
file) or `"loadObjs"` (binary files at their addresses). Without them it debugs what the
machine is running.

## What works

```{index} DeZog; breakpoints, DeZog; watchpoints, DeZog; banks
```

- Breakpoints on source lines, with conditions, hit counts, logpoints (`LOGPOINT`) and
  assertions (`ASSERTION`). DeZog works the conditions out itself, each time the machine
  stops at one.
- **Step Over**, **Step Into**, **Step Out**, **Continue** and **Pause**.
- Registers, memory, the call stack and the memory banks. A breakpoint on a line in a paged
  bank stops only when that bank is paged in.
- Watchpoints (`WPMEM` in the source), and saving and restoring the machine state
  (`-state save` and `-state restore` in DeZog's debug console): DeZog 3.8 and later.
- On the ZX Spectrum Next: the sprites, their patterns and palettes, and the Next registers.

Not available: DeZog's code coverage, its count of T-states, and stepping back through
everything the program did. DeZog asks those only of its own simulator and of ZEsarUX.
Its **Step Back** goes back over the steps you took.

## Machines

```{index} DeZog; machines
```

DeZog knows the memory of the ZX Spectrum 48K, 128K and Next. Xpeccy+ shows the 48K (and
ALF TV Game) as a 48K, the Next as a Next, and every other machine as a 128K. What a 128K
does not have is shown by its own number, which DeZog marks as an unknown bank: RAM banks
past 7 (Pentagon 1024, Scorpion, ZX Evolution), the +2A/+3's all-RAM paging, and a ROM
other than the two of the 128K (TR-DOS). Breakpoints on an address and stepping work on
every machine.

After switching to another machine, start the debug session again.

## DeZog and the Debugger

```{index} DeZog; Debugger window
```

While DeZog is connected, the machine's stops are DeZog's: a breakpoint stops the
machine and VS Code shows where, and the [Debugger](debugger.md) window does not open by
itself. Your own breakpoints from the [Breakpoints panel](debugger-breakpoints.md) still
stop the machine; DeZog shows them as *Xpeccy+ breakpoint* with what they caught. DeZog's
breakpoints are not in that panel.

While the machine is stopped you can open the Debugger to look at it. While the Debugger
is open, **Continue** and the steps in VS Code stop at once with a note; close the Debugger
first.

## Without a window

```{index} DeZog; --bench
```

With `--bench` (see [Command line](command-line.md#running-with-no-window)) and
`--dezog-port`, Xpeccy+ runs with no window and DeZog, or a test of your own that speaks
DZRP, drives it. While DeZog holds the machine stopped, the frames of `--bench` do not
count.
