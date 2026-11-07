#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "xreplay.h"
#include "xstate.h"
#include "xlog.h"

int xreplay_on = 0;
int xreplay_mode = XRP_OFF;
int xreplay_hold = 0;

#define XRP_LOG	0x20000		// INs a frame may read: a tight IN loop at 14 MHz is ~25000

// one frame: the machine at its start and what the INs read since
typedef struct {
	xState* st;
	int ok;			// the snapshot can be run from
	long long tick;		// comp->tickTotal it was taken at
	unsigned media;		// x_media_writes then: a write since cannot be undone
	unsigned char* log;
	int len;
} xrpFrame;

static xrpFrame frm[2];		// [0] the frame before, [1] this one
static int feedPos;		// INs a redraw has been fed, over both logs
static int feedFail;		// it asked for one past their end

static int frame_init(xrpFrame* f) {
	if (!f->st) f->st = xstate_create();
	if (!f->log) f->log = (unsigned char*)malloc(XRP_LOG);
	return f->st && f->log;
}

void xreplay_forget(void) {
	frm[0].ok = 0;
	frm[1].ok = 0;
	if (xreplay_mode == XRP_LOG) xreplay_mode = XRP_OFF;
}

// the TSU keeps its layers apart only while a redraw may want them
void xreplay_arm(int on) {
	if (!xreplay_on != !on) xlog(XLG_CORE, XLL_INFO, "redraw snapshots %s", on ? "on" : "off");
	xreplay_on = on ? 1 : 0;
	tslLayersApart = xreplay_on;
	if (!xreplay_on) xreplay_forget();
}

void xreplay_frame(Computer* comp) {
	if (!xreplay_on) return;
	if (x_runahead) return;		// a pass that is thrown away, a redraw's included
	// a machine run fast, or put no pixels out, is not stopped to be looked at:
	// a stop after it shows the change from the next frame instead
	if (xreplay_hold || comp->vid->nodraw) {
		xreplay_forget();
		return;
	}
	xrpFrame tmp = frm[0];
	frm[0] = frm[1];
	frm[1] = tmp;
	xrpFrame* f = &frm[1];
	f->ok = 0;
	f->len = 0;
	xreplay_mode = XRP_OFF;
	if (!frame_init(f)) return;
	if (!xstate_safe(comp) || rzx_playing || rzx_recording) return;
	if (!xstate_save_as_is(f->st, comp)) return;		// its run is thrown away: no fm wanted
	f->tick = comp->tickTotal;
	f->media = x_media_writes;
	f->ok = 1;
	xreplay_mode = XRP_LOG;
}

int xreplay_in(int val) {
	if (xreplay_mode == XRP_LOG) {
		if (x_runahead) return val;		// read by a frame run ahead: not history
		xrpFrame* f = &frm[1];
		if (f->len < XRP_LOG) {
			f->log[f->len++] = val & 0xff;
		} else {
			f->ok = 0;
			xreplay_mode = XRP_OFF;
		}
		return val;
	}
	// a redraw: the frame before, then this one
	int n = feedPos++;
	if (n < frm[0].len) return frm[0].log[n];
	n -= frm[0].len;
	if (n < frm[1].len) return frm[1].log[n];
	feedFail = 1;
	return val;
}

// The fields the machine's owner sets from outside it: not history, so the run is
// not expected to give them back, and they are left out of the comparison
typedef struct {
	int brk, brkpre, frm, dbg, brkn, frmCount;
	xBrkHit brkhit[BRK_HITS];
	unsigned debug, tail;
} xrpOwner;

static void owner_get(xrpOwner* o, Computer* comp) {
	o->dbg = comp->flgDBG;
	o->brk = comp->flgBRK;
	o->brkpre = comp->flgBRKPRE;
	o->frm = comp->flgFRM;
	o->brkn = comp->brkn;
	memcpy(o->brkhit, comp->brkhit, sizeof(o->brkhit));
	o->frmCount = comp->frmCount;		// counted by the emulation loop, not the machine
	o->debug = comp->vid->debug;
	o->tail = comp->vid->tail;
}

static void owner_set(Computer* comp, const xrpOwner* o) {
	comp->flgDBG = o->dbg;
	comp->flgBRK = o->brk;
	comp->flgBRKPRE = o->brkpre;
	comp->flgFRM = o->frm;
	comp->brkn = o->brkn;
	memcpy(comp->brkhit, o->brkhit, sizeof(o->brkhit));
	comp->frmCount = o->frmCount;
	comp->vid->debug = o->debug;
	comp->vid->tail = o->tail;
}

static xState* stNow = NULL;
static xState* stEnd = NULL;
static unsigned char* picSave = NULL;	// both image buffers, to put back if the run fails
static int picCap = 0;
static char why[160] = "";		// the last redraw's outcome, in words

const char* xreplay_why(void) {
	return why;
}

#define REFUSE(...) do { snprintf(why, sizeof(why), __VA_ARGS__); return 0; } while (0)

int xreplay_redraw(Computer* comp) {
	// the lines already drawn by the TSU go with the layers as now set, redraw or not
	int tsl = (comp->hw->id == HW_TSLAB);
	if (tsl) tsl_recompose(comp->vid);
	if (!xreplay_on) REFUSE("off");
	if (!frm[0].ok || !frm[1].ok) REFUSE("no snapshot of the frame before");
	if (!xstate_safe(comp)) REFUSE("the tape or a disk command is running");
	if (rzx_playing || rzx_recording) REFUSE("an RZX is playing or recording");
	if (comp->vid->nodraw) REFUSE("the picture is not being drawn");
	if (frm[0].media != x_media_writes) REFUSE("a disk was written since");
	if (frm[0].tick > comp->tickTotal) REFUSE("the machine went back in time");
	if (!stNow) stNow = xstate_create();
	if (!stEnd) stEnd = xstate_create();
	if (picCap < 2 * bufSize) {
		unsigned char* p = (unsigned char*)realloc(picSave, 2 * bufSize);
		if (!p) REFUSE("no memory");
		picSave = p;
		picCap = 2 * bufSize;
	}
	// A register that is only a copy of others (the Z80's F is made from its flags
	// when it is asked for) is as fresh as the last reader left it: both sides of
	// the comparison get it filled in the same way
	cpuGetRegs(comp->cpu);
	if (!stNow || !stEnd || !xstate_save(stNow, comp)) REFUSE("no memory");
	xrpOwner own;
	owner_get(&own, comp);
	vBufState bufs;
	vid_buf_get(&bufs);
	memcpy(picSave, bufs.scr, bufSize);
	memcpy(picSave + bufSize, bufs.buf, bufSize);
	long long target = comp->tickTotal;
	if (!xstate_load(frm[0].st, comp)) REFUSE("the machine changed");
	// drawn the way the owner draws now: the debugger keeps to one buffer
	comp->vid->debug = own.debug;
	if (tsl) tsl_recompose(comp->vid);
	// The snapshot was taken after the instruction the frame started in, which had
	// drawn its first dots already: they come from the frame as it was finished.
	// (Stepped over in the debugger, that frame never got to bufimg - but the dots
	// are in the blanking.)
	memcpy(scrimg, bufs.buf, comp->vid->ray.ptr - scrimg);
	feedPos = 0;
	feedFail = 0;
	int guard = 1 << 20;
	xreplay_mode = XRP_FEED;
	x_runahead = 1;
	while ((comp->tickTotal < target) && (guard-- > 0))
		compExec(comp);
	x_runahead = 0;
	xreplay_mode = XRP_OFF;
	int ok = (comp->tickTotal == target) && !feedFail && (feedPos == frm[0].len + frm[1].len);
	if (ok) {
		owner_set(comp, &own);
		cpuGetRegs(comp->cpu);
		ok = xstate_save_as_is(stEnd, comp);	// compared without the sound parts
	} else {
		snprintf(why, sizeof(why), "the run ends at T %" PRId64 " of %" PRId64 ", %i INs of %i%s",
			(int64_t)comp->tickTotal, (int64_t)target, feedPos, frm[0].len + frm[1].len, feedFail ? ", past the log" : "");
	}
	if (ok) {
		const unsigned char* data = NULL;
		size_t first = 0;
		xstate_bytes(stEnd, &data, NULL);
		// The sound devices are run by the sound mixer, outside the machine's own
		// steps, so a run does not move them. What the cpu read from them came
		// from the log anyway.
		size_t diff = xstate_diff_parts(stNow, comp, data, &first, XST_PART_SOUND);
		if (diff) {
			size_t inner = 0;
			int part = xstate_chunk_at(stNow, first, &inner);
			snprintf(why, sizeof(why), "the run ends %u bytes away, the first in part %i at %u",
				(unsigned)diff, part, (unsigned)inner);
			ok = 0;
		}
	}
	if (!ok) {
		vid_buf_set(&bufs);
		memcpy(bufs.scr, picSave, bufSize);
		memcpy(bufs.buf, picSave + bufSize, bufSize);
	}
	xstate_load(stNow, comp);		// the ray by its offset into scrimg, as it now is
	if (ok) snprintf(why, sizeof(why), "redrawn");
	xreplay_mode = frm[1].ok ? XRP_LOG : XRP_OFF;
	return ok;
}
