#pragma once

#include "../libxpeccy/spectrum.h"

// DeZog's remote: the DZRP protocol (2.2) on a TCP port of the loopback, so that
// DeZog in VS Code debugs the machine that runs here. One client at a time. Off
// unless Options or --dezog turn it on; off, nothing of it exists.

// listen as conf.dezog and the command line say, or stop listening
void dezog_apply();
// the command line's word for this run: on, on that port (0: the config's)
void dezog_cli(int port);
// stop listening and let the client go (at exit)
void dezog_close();
// the port it listens on, 0: off
int dezog_port();
// a client is attached: the machine's stops are its own
bool dezog_attached();
// The emulation's thread, under emu_lock: a breakpoint stopped the machine. 1 when
// DeZog takes the stop (the machine waits for it, no debugger window)
int dezog_stop(Computer*);
