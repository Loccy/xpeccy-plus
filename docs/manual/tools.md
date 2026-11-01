# Tools menu

```{index} Tools
```

The **Tools** menu holds the windows for looking into the machine and changing it, and the
settings. The right-click menu over the picture has the same windows under **Tools**, and
**Options...** at its foot.

- {icon}`cheats` **Cheats...** - trainers and pokes for the program that runs; see
  [Cheats](cheats.md).
- {icon}`bug` **Debugger** - stops the machine and shows what is inside it; see
  [Debugger](debugger.md).
- {icon}`objective` **Watcher** - registers and memory, followed while the machine runs; see
  [Watcher](debugger-watcher.md).
- {icon}`rulers` **Screen** and {icon}`note` **Sound Chips** - two panels of the Debugger in
  windows of their own, described below.
- {icon}`memory` **Heat Map** - the Debugger's heat map in a window of its own; see
  [Heat Map window](debugger-heatmap.md#heat-map-window).
- {icon}`other` **Options...** - the settings; see [Options](options.md). On macOS it is
  **Settings...** in the application menu.

## Screen and Sound Chips windows

```{index} Screen window, Sound Chips window, Show in its own window
```

![The Screen window](images/dbg-screen-window.png)

![The Sound Chips window](images/dbg-sound-window.png)

**Tools > Screen** and **Tools > Sound Chips** move the **Screen** and **Sound Chip**
panels out of the Debugger into windows of their own. The same command again puts the
panel back. In the Classic preset the keys are Alt+S and Alt+A; the Modern preset has no
keys for them.

The button at the top right of each panel, {icon}`display` **Show in its own window**, does
the same. In the window it is pressed in; click it again, or close the window, to put the
panel back in the Debugger. While the panel is out, its tab in the Debugger says so, with a
**Bring it back** or **Bring them back** button.

The windows work without the Debugger: they follow the machine while the machine runs, one
picture a frame (in fast mode, one in ten frames). The settings are the same as in the
panel. Each window keeps its place and size, and a window that was open when you quit
opens again at the next start.

```{toctree}
:maxdepth: 2

cheats
```
