#pragma once

#include <vector>
#include <string>

#if defined(__linux) || defined(__BSD)
	#include <sys/ioctl.h>
	#include <sys/soundcard.h>
	#include <fcntl.h>
#endif

#include "xcore.h"

enum xSoundOutput {
	xOutputNone = 0,
	xOutputOss,
	xOutputAlsa,
	xOutputSDL,
	xOutputWave
};

typedef struct {
	int id;
	const char* name;
	int (*open)();
	void (*play)();		// start playing what has been buffered so far
	void (*close)();
} OutSys;

// The ring is sbuf in sound.cpp, 4 bytes to a frame - 341 ms at 48kHz. It is
// sized well over the latency ceiling: a bluetooth headset needs far more of a
// buffer than a wired one, and the ring has to hold the worst of them.
#define SND_RING_SIZE	0x10000
#define SND_RING_MASK	(SND_RING_SIZE - 1)

// Filling stops here. Past it the writer would overwrite sound that has not
// been played yet.
#define SND_RING_HIGH_WATER	(SND_RING_SIZE * 7 / 8)

// One audio callback block, in ms. The callback asks for a whole block at once,
// so the ring can never hold less than this without clicking: the block is the
// floor under conf.snd.latency, and the target is at least two of them.
#define SND_BLOCK_MS	5
#define SND_LATENCY_MIN	(2 * SND_BLOCK_MS)
#define SND_LATENCY_MAX	150
// where a fresh install starts. Above where the regulator comes to rest, on
// purpose: Auto walks a too-high value quietly down, while too low a one is
// heard before it climbs back.
#define SND_LATENCY_DEF	30

// the highest rate the app offers. the ring holds fewer ms the higher this
// goes, so it is what the size has to be checked against (see pacing.cpp)
#define SND_MAX_RATE		48000

// Output rates offered, highest first, 0 terminates. It lives here rather than
// in the options dialog because the config parser has to check against the same
// list - a rate this build does not offer would leave the box with nothing
// selected. 11025 and 22050 were dropped in 2026.4: every audio engine now runs
// at 44100 or 48000, so a lower rate only bought a resample on the way out plus
// the fold-back our box decimation cannot filter.
//
// Auto is bounded by the same list, so a device running at anything else - 96000,
// say - is still asked for the nearest of these and still resamples. Following it
// exactly would mean sizing the ring at run time, which SND_MAX_RATE rules out.
extern const int sndRateTab[];
int sndRateSupported(int);

extern OutSys sndTab[];
extern OutSys* sndOutput;

extern long long nsPerSampleFixed;
void sndUpdateSpeed(void);
// VSync's share of the speed: the machine run at the display's rate, and the
// small steer that holds the ring (pacing.cpp). 1.0: none.
void sndSetLockScale(double);
// emulated ns one output sample stands for
double sndNsPerOutSample();

// The scope's own capture, at the sub-sample rate - 32 of these go into one sample
// of what reaches the speakers. The output ring cannot serve the scope on its own:
// it is decimated, and it stops being filled the moment the debugger takes the
// machine, which is exactly when a waveform is worth looking at.
int snd_scope(short*, int);		// the newest sub-samples, oldest first
double snd_scope_rate();		// how many of them a second
// the mixer's history, back to what a start leaves it at (the bench)
void snd_pipe_reset();
void snd_scope_step(Computer*, int);	// carry the capture over ns of a held machine

void sndInit();
void addOutput(std::string, bool(*)(),void(*)(),void(*)());
void setOutput(const char*);

void sndClose();
int sndSync(Computer*);
// the fixed ns from acc (the time made since the last sub-sample) past which the
// sub-sample that meets the pacer's need is due; the next one's when nothing is owed
long long snd_need_room(long long acc);
void snd_put(sndPair);
// what is queued to play is not what comes next: the machine's time ran on
// unheard, or it was loaded or reset. Emulation thread, or under emu_lock.
void snd_drop();
// The sound thread makes the samples of what sndSync() queues. The emulation
// thread hands its cycle's over at the end of it; snd_sync_wait() waits until
// everything handed over is in the ring, the scope and the wav.
void snd_q_flush();
void snd_sync_wait();
// the chip whose state is heard: the sound thread's copy when it mixes the machine
// itself (the levels, peaks and envelope the debugger shows), else the machine's
aymChip* snd_heard_chip(Computer*, int);
// A YM2203's fm is made on the sound thread only: the machine's core runs its timers
// (fm_stale). The debugger's view of it, from the copy that made the sound when there
// is one (under the sound thread's own lock); else the machine's, as it always was
void snd_fm_view(Computer*, aymChip*, fmChan*);
// The copy's fm as it stands, for rewind to keep with a snapshot (on the sound thread,
// in a snd_q_call); NULL when it has none
void* snd_fm_capture();
void snd_fm_capture_free(void*);
size_t snd_fm_capture_bytes(const void*);
// A snapshot put back: the copy seeded from it at once, a stale fm from what was
// captured there. Emulation thread
void snd_fm_reseed(Computer*, const void*);
// A snapshot shown: the machine's stale fm made whole from what was captured there
void snd_fm_restore(Computer*, const void*);
// Fast mode and sound off make no sub-samples to carry the machine's time to the sound
// thread's copy of its chips: this hands it over in pieces, and holds the machine
// while the copy is too far behind. Emulation thread
void snd_mirror_keep_up(Computer*);
// fn(arg) runs on the sound thread, after the sub-samples queued so far
void snd_q_call(void (*fn)(long long), long long arg);
int snd_queued_bytes();		// handed over, not yet in the ring
int sndGetRingDistance();
int snd_ring_fill_pos();
int snd_ring_byte(int);
int sndGetRingTargetBytes();
int sndPlaybackActive();
void sndAutoTick(long long);

int snd_wav_open(const char*);
wavHead wav_prepare(unsigned int rate, unsigned short chans);
void snd_wav_close();
void snd_wav_write(sndPair);

std::string sndGetName();
void sndDebug();
