#pragma once

#include "../libxpeccy/spectrum.h"

// Fast loading: the machine runs flat out while a loader reads the tape.
// frame() is called at the end of every emulated frame and decides it.
void fastload_frame(Computer*);
// Fast mode the emulator holds itself, with a picture drawn fast.fps times a
// second of host time (only when draw allows it); release gives the machine back.
void fast_hold_frame(Computer*, long long* drawnAt, int draw);
void fast_hold_release(Computer*);
long long fast_refresh_ns();		// host ns between two pictures of fast mode
// The user's fast mode: which frames are drawn, and a pause asked for meanwhile.
// Called at the end of every emulated frame, after the holders above.
void fast_picture_frame(Computer*);
int fast_ffwd_frame();
int fast_picture_busy();		// frames are being left undrawn
// Frame skip (Options > Video): whether a frame may be left undrawn now, the option
// aside (not run ahead, recorded, watched by a breakpoint, loaded or wound back)
int frame_skip_allowed(Computer*);
int frame_skip_ok();			// ...as the emulation found it at the last frame end (any thread)
int frame_skipping();			// in a stretch of frames left undrawn: anti-flicker stands down
void frame_skip_stop(Computer*);	// the frame left undrawn or held back is drawn from here (the debugger takes it)
void fast_picture_hold(int);		// the bench draws them itself
// let the machine go now - the debugger is about to take it
void fastload_stop(Computer*);
// the machine is being run through a load: the picture is held
int fastload_busy();
// the tape has moved in a way a wind back cannot undo: a flash load
void fastload_forget();
// keep fast loading from taking the machine (the bench runs its own mode)
void fastload_hold(int);
// after every opcode while fastload_on: skips an edge loop, returns the ns it moved on
extern int fastload_on;
int fastload_step(Computer*);
// --bench-loops: skip edge loops in any fast mode - 1 exactly, 2 as edge
// detection, 3 exactly and checked against running them
void fastload_bench(int mode);
