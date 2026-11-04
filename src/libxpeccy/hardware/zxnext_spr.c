// The ZX Spectrum Next's sprites, as its FPGA core (3.02.03) draws them
// (video/sprites.vhd): the ports and the nextreg mirror, the attribute and
// pattern memories, and the pass that fills a line buffer while the other one is
// shown.
//
// Time. The pass runs state by state, one CLK_28 each, as the core's does: START,
// a QUALIFY per sprite (which reads its attributes then), a PROCESS per position
// a drawn sprite walks. State k of the pass that starts with whc = 511 on edge R
// lasts from R + 1 + k to R + 2 + k; its variables change on the falling edge in
// it. Everything that writes the memories runs the pass up to its own moment
// first (nxs_run), so a write lands between the states it falls between. Times
// here are in half CLK_28 (a falling edge is odd).

#include <string.h>

#include "zxnext.h"

#define SP (NX.spr)

enum {NXS_IDLE = 0, NXS_START, NXS_QUALIFY, NXS_PROCESS};

// the abs dot raw H (hact - 32) of each line: whc = 511 there (zxula_timing.vhd 476-488)
static long long nxs_swap_after(Computer* comp, long long after) {
	const nxTiming* t = nx_tim(comp);
	long long c = (NX.frameBase + t->hact - 32) * 4 + 2;	// the swap edge of the ray's line
	long long L4 = (long long)t->L * 4;
	return c + (nx_fdiv(after - c, L4) + 1) * L4;	// the first one past after
}

// wvc during the dot d (zxula_timing.vhd 494-507): it steps at raw H hact - 47
static int nxs_wvc(Computer* comp, long long d) {
	const nxTiming* t = nx_tim(comp);
	int h, v;
	nx_raw_at(comp, d * 4, &h, &v);
	int line = (h >= t->hact - 47) ? v : v - 1;
	int F = nx_lines(comp);
	if (line < 0) line += F;
	int vact = nx_vact(comp);
	if (line >= vact - 34) return (line - vact + 32) & 0x1ff;
	return (NX.wvcLast + 1 + line) & 0x1ff;		// still counting on from the frame before
}

// a status flag event at half-CLK_28 time t
static void nxs_flag(Computer* comp, int b, long long t) {
	if (SP.flagT0[b] < 0) SP.flagT0[b] = t;
	SP.flagT1[b] = t;
}

// QUALIFY: sprite idx's attributes, as they are now (sprites.vhd 756-950)
static void nxs_qualify(Computer* comp) {
	unsigned char* a = SP.attr[SP.idx];
	int a0 = a[0], a1 = a[1], a2 = a[2], a3 = a[3], a4 = a[4];
	int e = (a3 >> 6) & 1;
	int rel = e && ((a4 & 0xc0) == 0x40);
	int c0 = a0, c1 = a1, c2 = a2, c3 = a3, c4 = a4;
	if (rel) {
		int x0 = SP.aRot ? a1 : a0;
		int y0 = SP.aRot ? a0 : a1;
		int x1 = (SP.aRot ^ SP.aXm) ? ((-x0) & 0xff) : x0;
		int y1 = SP.aYm ? ((-y0) & 0xff) : y0;
		int x2, y2;
		switch (SP.aXs) {
			case 0: x2 = (x1 & 0x80) ? (x1 | 0x100) : x1; break;
			case 1: x2 = (x1 << 1) & 0x1ff; break;
			case 2: x2 = (x1 << 2) & 0x1ff; break;
			default: x2 = (x1 << 3) & 0x1ff; break;
		}
		switch (SP.aYs) {
			case 0: y2 = (y1 & 0x80) ? (y1 | 0x100) : y1; break;
			case 1: y2 = (y1 << 1) & 0x1ff; break;
			case 2: y2 = (y1 << 2) & 0x1ff; break;
			default: y2 = (y1 << 3) & 0x1ff; break;
		}
		int x3 = (SP.aX + x2) & 0x1ff;
		int y3 = (SP.aY + y2) & 0x1ff;
		int pal = (a2 & 1) ? ((SP.aPal + (a2 >> 4)) & 15) : (a2 >> 4);
		int rxm = SP.aRot ? (((a2 >> 2) ^ (a2 >> 1)) & 1) : ((a2 >> 3) & 1);
		int rym = SP.aRot ? (((a2 >> 3) ^ (a2 >> 1)) & 1) : ((a2 >> 2) & 1);
		c0 = x3 & 0xff;
		c1 = y3 & 0xff;
		if (!SP.aType)
			c2 = (pal << 4) | (a2 & 0x0e) | (x3 >> 8);
		else
			c2 = (pal << 4) | ((SP.aXm ^ rxm) << 3) | ((SP.aYm ^ rym) << 2) | ((SP.aRot ^ ((a2 >> 1) & 1)) << 1) | (x3 >> 8);
		c3 = ((SP.aVis & (a3 >> 7)) << 7) | 0x40 | (a3 & 0x3f);
		if (!SP.aType)
			c4 = (SP.aH << 7) | (((a4 >> 5) & 1) << 6) | (a4 & 0x1e) | (y3 >> 8);
		else
			c4 = (SP.aH << 7) | (((a4 >> 5) & 1) << 6) | (SP.aXs << 3) | (SP.aYs << 1) | (y3 >> 8);
	}
	int y8 = e ? (c4 & 1) : 0;
	int cy = (y8 << 8) | c1;
	int cx = ((c2 & 1) << 8) | c0;
	int ch = (c4 >> 7) & e;
	int n6 = (c4 >> 6) & 1 & ch;
	int pat = ((a3 & 0x3f) << 1) | n6;
	if (rel && (a4 & 1)) pat = (pat + SP.aPat) & 0x7f;
	int yraw = (SP.vcount - cy) & 0x1ff;
	int yy = e ? ((c4 >> 1) & 3) : 0;
	int yoff = yraw;
	if (yy) {
		int s = (yraw & 0x100) ? (yraw | ~0x1ff) : yraw;	// 9-bit arithmetic shift
		yoff = (s >> yy) & 0x1ff;
	}
	int yi = (c2 & 4) ? (~yoff & 15) : (yoff & 15);
	int rot = (c2 >> 1) & 1;
	int xme = ((c2 >> 3) ^ rot) & 1;		// rotation inverts the x mirror
	int xi = xme ? 15 : 0;
	SP.addr = (pat >> 1) << 8 | (rot ? ((xi << 4) | yi) : ((yi << 4) | xi));
	SP.adelta = xme ? (rot ? 0x3ff0 : 0x3fff) : (rot ? 0x0010 : 0x0001);
	SP.hcount = cx;
	SP.width = 0;
	int xx = e ? ((c4 >> 3) & 3) : 0;
	SP.wdelta = 8 >> xx;
	SP.xwrap = (0x1f << xx) & 0x1f;
	SP.paloff = c2 >> 4;
	SP.visible = c3 >> 7;
	SP.yoff = (yoff >> 4) & 0x1f;
	SP.bits4 = (ch << 1) | (pat & 1);
	if (!rel) {
		SP.aType = (a4 >> 5) & e;
		SP.aH = (a4 >> 7) & e;
		SP.aVis = a3 >> 7;
		SP.aX = cx;
		SP.aY = cy;
		SP.aPat = pat;
		SP.aPal = a2 >> 4;
		if (e && (a4 & 0x20)) {
			SP.aRot = rot;
			SP.aXm = (a2 >> 3) & 1;
			SP.aYm = (a2 >> 2) & 1;
			SP.aXs = (a4 >> 3) & 3;
			SP.aYs = (a4 >> 1) & 3;
		} else {
			SP.aRot = SP.aXm = SP.aYm = SP.aXs = SP.aYs = 0;
		}
	}
	SP.idx = (SP.idx + 1) & 0x7f;
}

// All 128 sprites as QUALIFY takes them: the same decode, the anchor carried from one to
// the next, the line left out. Kept beside nxs_qualify so the two change together.
void next_sprites(Computer* comp, nxSpriteView* out) {
	int aType = 0, aH = 0, aVis = 0, aX = 0, aY = 0, aPat = 0, aPal = 0;
	int aRot = 0, aXm = 0, aYm = 0, aXs = 0, aYs = 0, anchor = -1;
	for (int i = 0; i < 128; i++) {
		unsigned char* a = SP.attr[i];
		int a0 = a[0], a1 = a[1], a2 = a[2], a3 = a[3], a4 = a[4];
		int e = (a3 >> 6) & 1;
		int rel = e && ((a4 & 0xc0) == 0x40);
		int c0 = a0, c1 = a1, c2 = a2, c3 = a3, c4 = a4;
		if (rel) {
			int x0 = aRot ? a1 : a0;
			int y0 = aRot ? a0 : a1;
			int x1 = (aRot ^ aXm) ? ((-x0) & 0xff) : x0;
			int y1 = aYm ? ((-y0) & 0xff) : y0;
			int x2 = aXs ? (x1 << aXs) & 0x1ff : ((x1 & 0x80) ? (x1 | 0x100) : x1);
			int y2 = aYs ? (y1 << aYs) & 0x1ff : ((y1 & 0x80) ? (y1 | 0x100) : y1);
			int x3 = (aX + x2) & 0x1ff;
			int y3 = (aY + y2) & 0x1ff;
			int pal = (a2 & 1) ? ((aPal + (a2 >> 4)) & 15) : (a2 >> 4);
			int rxm = aRot ? (((a2 >> 2) ^ (a2 >> 1)) & 1) : ((a2 >> 3) & 1);
			int rym = aRot ? (((a2 >> 3) ^ (a2 >> 1)) & 1) : ((a2 >> 2) & 1);
			c0 = x3 & 0xff;
			c1 = y3 & 0xff;
			if (!aType)
				c2 = (pal << 4) | (a2 & 0x0e) | (x3 >> 8);
			else
				c2 = (pal << 4) | ((aXm ^ rxm) << 3) | ((aYm ^ rym) << 2) | ((aRot ^ ((a2 >> 1) & 1)) << 1) | (x3 >> 8);
			c3 = ((aVis & (a3 >> 7)) << 7) | 0x40 | (a3 & 0x3f);
			if (!aType)
				c4 = (aH << 7) | (((a4 >> 5) & 1) << 6) | (a4 & 0x1e) | (y3 >> 8);
			else
				c4 = (aH << 7) | (((a4 >> 5) & 1) << 6) | (aXs << 3) | (aYs << 1) | (y3 >> 8);
		}
		int y8 = e ? (c4 & 1) : 0;
		int ch = (c4 >> 7) & e;
		int pat = ((a3 & 0x3f) << 1) | ((c4 >> 6) & 1 & ch);
		if (rel && (a4 & 1)) pat = (pat + aPat) & 0x7f;
		nxSpriteView* v = &out[i];
		v->x = ((c2 & 1) << 8) | c0;
		v->y = (y8 << 8) | c1;
		v->pat = pat;
		v->bits4 = ch;
		v->pal = c2 >> 4;
		v->xm = (c2 >> 3) & 1;
		v->ym = (c2 >> 2) & 1;
		v->rot = (c2 >> 1) & 1;
		v->xs = e ? (c4 >> 3) & 3 : 0;
		v->ys = e ? (c4 >> 1) & 3 : 0;
		v->vis = c3 >> 7;
		v->rel = rel;
		v->anchor = rel ? anchor : -1;
		v->unified = rel ? aType : (a4 >> 5) & e;
		v->ext = e;
		if (!rel) {
			anchor = i;
			aType = (a4 >> 5) & e;
			aH = (a4 >> 7) & e;
			aVis = a3 >> 7;
			aX = v->x;
			aY = v->y;
			aPat = pat;
			aPal = a2 >> 4;
			if (e && (a4 & 0x20)) {
				aRot = (c2 >> 1) & 1;
				aXm = (a2 >> 3) & 1;
				aYm = (a2 >> 2) & 1;
				aXs = (a4 >> 3) & 3;
				aYs = (a4 >> 1) & 3;
			} else {
				aRot = aXm = aYm = aXs = aYs = 0;
			}
		}
	}
}

// PROCESS: one position (sprites.vhd 951-995). The pattern byte was read on the
// state's rising edge, the line buffer written in it; the counters move on its
// falling edge.
static void nxs_process(Computer* comp) {
	int addr = SP.addr;
	int pa = (SP.bits4 & 2) ? ((addr & 0x3f00) | ((SP.bits4 & 1) << 7) | ((addr >> 1) & 0x7f)) : addr;
	int pd = SP.pat[pa & 0x3fff];
	int nib = (addr & 1) ? (pd & 15) : (pd >> 4);
	int tr = NX.nr[0x4b];
	int data = (SP.bits4 & 2) ? ((SP.paloff << 4) | nib) : (((((pd >> 4) + SP.paloff) & 15) << 4) | (pd & 15));
	if (SP.hcount < 320) {
		int we = (SP.bits4 & 2) ? (nib != (tr & 15)) : (pd != tr);
		unsigned short* b = &SP.buf[SP.passBuf][SP.hcount];
		int there = (*b >> 8) & 1;
		if (we && there) nxs_flag(comp, 0, SP.f2);	// collision, whatever the priority
		if (we && (!(NX.nr[0x15] & 0x40) || !there))
			*b = 0x100 | data;
	}
	int wn = (SP.width + SP.wdelta) & 0xff;
	SP.hcount = (SP.hcount + 1) & 0x1ff;
	if ((wn ^ SP.width) & 8) SP.addr = (SP.addr + SP.adelta) & 0x3fff;
	SP.width = wn;
}

// one state of the pass
static void nxs_state(Computer* comp) {
	switch (SP.st) {
		case NXS_START:
			SP.idx = 0;
			SP.aVis = 0;
			SP.st = NXS_QUALIFY;
			break;
		case NXS_QUALIFY:
			nxs_qualify(comp);
			if (SP.visible && !SP.yoff) SP.st = NXS_PROCESS;
			else if (!SP.idx) SP.st = NXS_IDLE;
			break;
		case NXS_PROCESS:
			nxs_process(comp);
			if ((SP.width & 0x80) || ((SP.hcount >= 320) && (((SP.hcount >> 4) & SP.xwrap) != SP.xwrap)))
				SP.st = SP.idx ? NXS_QUALIFY : NXS_IDLE;
			break;
	}
}

// The pass up to half-CLK_28 time t2: every state whose falling edge is at or
// before it, and the swaps on the way
void nxs_run(Computer* comp, long long t2) {
	for (;;) {
		// the next swap moves only with passSw or the raster; asked once a line, not a dot
		const nxTiming* tm = nx_tim(comp);
		int geo = (tm->L << 12) | tm->hact;
		if ((SP.swPass != SP.passSw) || (SP.swBase != NX.frameBase) || (SP.swGeo != geo)) {
			SP.swVal = nxs_swap_after(comp, SP.passSw);
			SP.swPass = SP.passSw;
			SP.swBase = NX.frameBase;
			SP.swGeo = geo;
		}
		long long sw = SP.swVal;
		if ((SP.st == NXS_IDLE) && (2 * sw - 1 > t2)) {
			SP.idleTo = 2 * sw - 1;
			return;
		}
		while (SP.st != NXS_IDLE) {
			long long f2 = 2 * SP.passR + 3 + 2 * SP.k;
			if ((f2 > t2) || (f2 >= 2 * sw)) break;
			if (SP.st == NXS_QUALIFY) {
				// Hidden sprites that are not relative, in a row: each QUALIFY of one
				// leaves only the anchor and fields the next QUALIFY sets again, so
				// all but the last are passed over - their states, one a CLK_28, kept
				long long end = (t2 < 2 * sw - 1) ? t2 : 2 * sw - 1;
				int lim = (int)((end - f2) / 2) + 1;
				int m = 0;
				if (lim > 128 - SP.idx) lim = 128 - SP.idx;
				while (m < lim) {
					unsigned char* a = SP.attr[SP.idx + m];
					if ((a[3] & 0x80) || ((a[3] & 0x40) && ((a[4] & 0xc0) == 0x40))) break;
					m++;
				}
				if (m > 1) {
					SP.idx += m - 1;
					SP.k += m - 1;
					f2 = 2 * SP.passR + 3 + 2 * SP.k;
				}
			}
			SP.lastK = SP.k;
			SP.f2 = f2;
			nxs_state(comp);
			SP.k++;
		}
		// the next pass starts on the swap edge sw; state P, the one before it,
		// still running there is the overtime flag (taken on the falling edge before)
		if (2 * sw - 1 > t2) {
			SP.idleTo = (SP.st == NXS_IDLE) ? 2 * sw - 1 : 0;
			break;
		}
		if (SP.lastK >= sw - SP.passR - 2) nxs_flag(comp, 1, 2 * sw - 1);
		SP.passR = sw - 2;
		SP.passSw = sw;
		SP.k = 1;
		SP.lastK = 0;
		SP.st = NXS_START;
		SP.dispOld = SP.passBuf ^ 1;		// shown up to the swap...
		SP.dispNew = SP.passBuf;		// ...and the line just drawn after it
		SP.passBuf ^= 1;
		SP.dispSwap = (sw - 2) >> 2;
		// the picture is drawn after the ray (zxnext_vid.c): the line shown up to here
		// is kept for it, then cleared as the display cleared it reading it
		if (!comp->vid->nodraw && !comp->vid->still) {
			NX.v.sprKeepNew ^= 1;
			memcpy(NX.v.sprKeep[NX.v.sprKeepNew], SP.buf[SP.passBuf], sizeof(SP.buf[0]));
			NX.v.sprKeepTo[NX.v.sprKeepNew] = SP.dispSwap;
		}
		memset(SP.buf[SP.passBuf], 0, sizeof(SP.buf[0]));
		SP.vcount = (nxs_wvc(comp, (sw - 2) >> 2) + 1) & 0x1ff;
	}
}


// == the ports and the mirror ==

// a mirror number change, and what the tie does with it (sprites.vhd 600-610, 653, 733)
static void nxs_mirror_set(Computer* comp, int v, long long w) {
	SP.mirror = v;
	if (NX.nr[0x09] & 0x10) {
		SP.tieW = w;
		SP.tieOld = SP.attrIdx;
		SP.attrIdx = (v & 0x7f) << 3;
		SP.patIdx = ((v & 0x3f) << 8) | ((v & 0x80) ? 0x80 : 0);
	}
}

// the port side moved to another sprite: with the tie, the mirror follows
static void nxs_attr_moved(Computer* comp) {
	if (NX.nr[0x09] & 0x10)
		SP.mirror = ((SP.attrIdx >> 3) & 0x7f) | (SP.patIdx & 0x80);
}

// nextreg 0x34-0x39, 0x75-0x79, latched on edge w
void nxs_nr(Computer* comp, int r, int v, long long w) {
	nxv_touch(comp);			// the attributes or the mirror: a still frame ends
	if (r == 0x34) {
		nxs_mirror_set(comp, v, w);
		return;
	}
	int i = (r & 0x0f) - 5;			// 0x35/0x75 -> byte 0
	nxs_run(comp, 2 * w - 1);		// the RAM is written on the falling edge before w
	SP.attr[SP.mirror & 0x7f][i] = v;
	if (r & 0x40)
		nxs_mirror_set(comp, (((SP.mirror & 0x7f) + 1) & 0x7f) | (SP.patIdx & 0x80), w);
}

// a port write whose iowr is first seen on edge w
void nxs_out(Computer* comp, int port, int v, long long w) {
	nxv_touch(comp);
	int lo = port & 0xff;
	if (lo == 0x57) {
		nxs_run(comp, 2 * w + 1);
		if (w <= SP.tieW + 1) {
			// mirror_num_change moves attr_index an edge after the mirror changed: a port
			// write up to that edge writes where the pointer was, and the tie wins over
			// its step (sprites.vhd 647-679)
			int ti = SP.tieOld & 7;
			SP.attr[(SP.tieOld >> 3) & 0x7f][ti] = v;
			return;
		}
		int i = SP.attrIdx & 7;
		SP.attr[(SP.attrIdx >> 3) & 0x7f][i] = v;
		if ((i == 4) || ((i == 3) && !(v & 0x40))) {
			SP.attrIdx = (((SP.attrIdx >> 3) + 1) & 0x7f) << 3;
			nxs_attr_moved(comp);
		} else {
			SP.attrIdx = (SP.attrIdx + 1) & 0x3ff;
		}
	} else if (lo == 0x5b) {
		nxs_run(comp, 2 * w + 1);
		SP.pat[SP.patIdx] = v;
		SP.patIdx = (SP.patIdx + 1) & 0x3fff;
	} else {				// 0x303B
		SP.attrIdx = (v & 0x7f) << 3;
		SP.patIdx = ((v & 0x3f) << 8) | ((v & 0x80) ? 0x80 : 0);
		nxs_attr_moved(comp);
	}
}

// port 0x303B read: the flags since the last read, cleared (sprites.vhd 977-995)
int nxs_status(Computer* comp, long long w) {
	long long r2 = 2 * w + 1;
	nxs_run(comp, r2);
	int s = 0;
	for (int b = 0; b < 2; b++) {
		if ((SP.flagT0[b] < 0) || (SP.flagT0[b] > r2)) continue;
		if (SP.flagT0[b] < r2) s |= 1 << b;	// one on the read's own edge is lost: the clear wins
		if (SP.flagT1[b] > r2)
			SP.flagT0[b] = SP.flagT1[b];	// a later one stays for the next read
		else
			SP.flagT0[b] = SP.flagT1[b] = -1;
	}
	return s;
}

int nxs_mirror(Computer* comp) {
	return SP.mirror & 0x7f;
}

void nxs_reset(Computer* comp) {
	SP.attrIdx = 0;
	SP.patIdx = 0;
	SP.mirror = 0;
	SP.flagT0[0] = SP.flagT0[1] = SP.flagT1[0] = SP.flagT1[1] = -1;
	SP.st = NXS_IDLE;
	SP.idleTo = 0;
	SP.idx = 0;
	SP.hcount = 0;
	SP.addr = 0;
	SP.width = 0;
	SP.aType = SP.aH = SP.aVis = SP.aRot = SP.aXm = SP.aYm = SP.aXs = SP.aYs = 0;
	SP.aX = SP.aY = 0;
	SP.aPat = SP.aPal = 0;
}
