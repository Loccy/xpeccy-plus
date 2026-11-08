// Headless ZX Spectrum Next bench: runs a program image the way the core's own
// simulation does, and logs what that simulation logs, so the two can be compared
// line by line.
//
//	zxnext-bench <sram.bin> <frames> [dump_addr dump_len] [outdir]
//
// sram.bin is the 2 MB SRAM image (ROM0 at 0: the program starts at PC 0 with the
// MMU as reset leaves it). "fw:rom[,divmmc[,mf]]" instead powers the machine on
// with those ROMs as the firmware hands over, the reset lines connected. The run ends after <frames> frames or at an OUT to port
// 0x0103. Written to outdir (default "."), in the simulation's formats:
//   probe.txt    every i/o write: frame hc vc hc_ula vc_ula cvc cpu_cycles port data
//   nextreg.txt  every nextreg write: frame hc vc cpu_cycles reg data
//   dump.bin     dump_len bytes of SRAM from dump_addr at the end (default 0x040000, 8192)
//   frame_N.txt  each finished picture: hex RGB333 per half-pixel, one line per row, the
//                rows and columns the core does not blank (N as the simulation numbers them)
// The expansion bus ROMCS pin is held as the simulation's testbench holds it. An OUT to
// 0x1003-0x1703 sets the joysticks and the mouse as that testbench does; 0x1803-0x1B03
// press and release a ps/2 key, which it cannot.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spectrum.h"
#include "cpu/Z80/z80.h"
#include "vfat.h"
#include <dirent.h>
#include <sys/stat.h>

static const char* base_name(const char* path) {
	const char* p = path;
	for (const char* s = path; *s; s++)
		if ((*s == '/') || (*s == 0x5c)) p = s + 1;
	return p;
}

static FILE* fprobe;
static FILE* faudio;
static FILE* fnr;
static int done;

// the ray as the core's counters have it now (the machine settled it for the log); a
// DMA write is logged at its own time (next_log_t)
static void ray(Computer* comp, int* frame, int* hc, int* vc, int* cvc) {
	if (next_log_t >= 0) {
		next_counters_at(comp, next_log_t, hc, vc, cvc);
		*frame = next_vsync_frame_at(comp, next_log_t + 1);
		return;
	}
	next_counters(comp, hc, vc, cvc);
	*frame = next_vsync_frame(comp, 1);		// the frame at the iowr edge, one CLK_28 on
}

static long long cycles(Computer* comp) {
	if (next_log_t >= 0) return next_log_cyc;
	return comp->tickTotal + comp->cpu->t - comp->next->holds;
}

// the UART0 Rx line's schedule (NXUART: a file in the simulation's own format), from the OUT to 0004
static long long uartOff[4096];
static int uartLv[4096], uartN = 0;

static void log_out(Computer* comp, int port, int val) {
	int frame, hc, vc, cvc;
	ray(comp, &frame, &hc, &vc, &cvc);
	fprintf(fprobe, "%d %d %d %d %d %d %lld %04X %02X\n", frame, hc, vc, hc, vc, cvc, cycles(comp), port & 0xffff, val & 0xff);
	// the inputs the simulation's testbench sets with these probes
	switch (port & 0xffff) {
		case 0x1003: comp->joy->state = (comp->joy->state & 0xf00) | (val & 0xff); break;
		case 0x1103: comp->joy->state = (comp->joy->state & 0xff) | ((val & 15) << 8); break;
		case 0x1203: comp->joyb->state = (comp->joyb->state & 0xf00) | (val & 0xff); break;
		case 0x1303: comp->joyb->state = (comp->joyb->state & 0xff) | ((val & 15) << 8); break;
		case 0x1403: comp->mouse->xpos = val & 0xff; break;
		case 0x1503: comp->mouse->ypos = val & 0xff; break;
		case 0x1603:
			comp->mouse->lmb = val & 1;
			comp->mouse->rmb = (val >> 1) & 1;
			comp->mouse->mmb = (val >> 2) & 1;
			break;
		case 0x1703: comp->mouse->wheel = val & 15; break;
		// ours only, the board's side the simulation has not: a ps/2 key goes down
		// (18, E0 19) or up (1A, E0 1B), a set 2 code
		case 0x1803: case 0x1903: case 0x1a03: case 0x1b03: {
			keyEntry e;
			memset(&e, 0, sizeof(e));
			e.atCode = (port & 0x100) ? (0xe0 | ((val & 0xff) << 8)) : (val & 0xff);
			if (port & 0x200) comp->hw->keyr(comp, &e);
			else comp->hw->keyp(comp, &e);
			break;
		}
	}
	if (((port & 0xffff) == 0x0004) && uartN) {
		long long trig = nx_now(comp) + 1;	// the edge iowr is first seen on, as the testbench takes it
		for (int i = 0; i < uartN; i++) nx_uart_line(comp, 0, trig + uartOff[i], uartLv[i]);
		uartN = 0;
	}
	if (((port & 0xffff) == 0x0103) && !getenv("NXNOEND")) done = 1;
	if (((port & 0xffff) == 0x0003) && faudio) {
		// the mix at a probe, as the testbench logs it (audio.txt): pcm_L pcm_R env_vol
		int l, r, e;
		next_audio(comp, &l, &r, &e);
		fprintf(faudio, "%d %d %d\n", l, r, e);
	}
}

// The simulation logs a nextreg write two CLK_28 after the request (the request is
// latched on the next edge, the log sees the latch an edge later): the pixel of
// request + 1 CLK_28 and the cpu edges up to it, which at 28 MHz is one more
static void log_nr(Computer* comp, int reg, int val, int late) {
	int hc, vc, cvc;
	if (next_log_at >= 0) {
		// the copper's: the pixel before its edge; no cpu edge count (-1)
		next_counters_at(comp, next_log_at - 1, &hc, &vc, &cvc);
		fprintf(fnr, "%d %d %d -1 %02X %02X\n", next_vsync_frame_at(comp, next_log_at), hc, vc, reg & 0xff, val & 0xff);
		return;
	}
	if (next_log_t >= 0) {
		next_counters_at(comp, next_log_t + 1, &hc, &vc, &cvc);
		fprintf(fnr, "%d %d %d %lld %02X %02X\n", next_vsync_frame_at(comp, next_log_t + 2), hc, vc,
			cycles(comp) + late + ((comp->next->speed == 3) ? 2 : 1), reg & 0xff, val & 0xff);
		return;
	}
	next_counters_ahead(comp, 1, &hc, &vc, &cvc);
	fprintf(fnr, "%d %d %d %lld %02X %02X\n", next_vsync_frame(comp, 2), hc, vc,
		cycles(comp) + late + ((comp->next->speed == 3) ? 2 : 1), reg & 0xff, val & 0xff);
}

// The picture just finished, as the simulation writes it: the image buffer holds the
// unblanked raster at its origin, two pixels a dot
static void dump_frame(Computer* comp, const char* out, int n, int rows, int cols) {
	char path[4096];
	snprintf(path, sizeof(path), "%s/frame_%d.txt", out, n);
	FILE* f = fopen(path, "w");
	if (!f) return;
	for (int y = 0; y < rows; y++) {
		unsigned int* row = (unsigned int*)(bufimg + y * bytesPerLine);
		for (int x = 0; x < cols; x++) {
			unsigned int c = row[x];
			int r = ((c & 0xff) * 7 + 127) / 255;
			int g = (((c >> 8) & 0xff) * 7 + 127) / 255;
			int b = (((c >> 16) & 0xff) * 7 + 127) / 255;
			fprintf(f, "%03X ", (r << 6) | (g << 3) | b);
		}
		fputc('\n', f);
	}
	fclose(f);
}

// what a program finds at its first instruction: registers, paging, nextregs, RAM
static void dump_state(Computer* comp, const char* out) {
	char path[4096];
	snprintf(path, sizeof(path), "%s/state.txt", out);
	FILE* f = fopen(path, "w");
	if (!f) return;
	CPU* c = comp->cpu;
	fprintf(f, "pc %04X sp %04X af %04X bc %04X de %04X hl %04X ix %04X iy %04X\n", c->regPC, c->regSP, c->regAF, c->regBC, c->regDE, c->regHL, c->regIX, c->regIY);
	fprintf(f, "af' %04X bc' %04X de' %04X hl' %04X i %02X im %d iff %d %d\n", c->regAFa, c->regBCa, c->regDEa, c->regHLa, c->regI, c->regIM, c->flgIFF1, c->flgIFF2);
	xNextState* n = comp->next;
	fprintf(f, "mmu %02X %02X %02X %02X %02X %02X %02X %02X\n", n->mmu[0], n->mmu[1], n->mmu[2], n->mmu[3], n->mmu[4], n->mmu[5], n->mmu[6], n->mmu[7]);
	fprintf(f, "7ffd %02X dffd %02X 1ffd %02X eff7 %02X e3 %02X automap %d fe %02X ff %02X 123b %02X speed %d\n", n->p7ffd, n->pdffd, n->p1ffd, n->peff7, n->pe3, n->dmHeld, n->pfe, n->pff, n->p123b, n->speed);
	for (int r = 0; r < 256; r += 16) {
		fprintf(f, "nr %02X:", r);
		for (int i = 0; i < 16; i++) fprintf(f, " %02X", n->nr[r + i]);
		fprintf(f, "\n");
	}
	fclose(f);
	snprintf(path, sizeof(path), "%s/ram.bin", out);
	f = fopen(path, "wb");
	if (!f) return;
	for (int a = 0; a < 0x200000; a++) {
		int pg = (a - 0x040000) >> 13;
		fputc(comp->mem->ramData[a], f);
		(void)pg;
	}
	fclose(f);
	snprintf(path, sizeof(path), "%s/pal.bin", out);
	f = fopen(path, "wb");
	if (f) {
		fwrite(n->pal, sizeof(n->pal), 1, f);
		fclose(f);
	}
}

// --switch: a Next run a while, then a 48K in the same Computer - the Next's cpu hooks
// must go with it, or every INT of the new machine runs the Next's acknowledge
static int switch_check(void) {
	xhost_time_fixed = 1759665600;
	Computer* comp = compCreate();
	compSetHardware(comp, "ZXNext");
	compSetBaseFrq(comp, 3.5);
	compReset(comp, RES_DEFAULT);
	printf("== switch\n");
	next_mmu_set(comp, 0, 0x11);
	next_mmu_set(comp, 1, 0x12);
	int mmu0 = 0x040000 + (0x11 << 13) + 0x123;
	int mmu1 = 0x040000 + (0x12 << 13) + 0x123;
	comp->mem->ramData[mmu0] = 0xa4;
	comp->mem->ramData[mmu1] = 0xa5;
	xAdr xa;
	int v0 = comp_peek(comp, 0x0123, MEM_VIEW_READ, &xa);
	printf("view mmu0: %02X @ %06X page %02X\n", v0, xa.abs, next_mmu_get(comp, 0));
	int v1 = comp_peek(comp, 0x2123, MEM_VIEW_READ, &xa);
	printf("view mmu1: %02X @ %06X page %02X\n", v1, xa.abs, next_mmu_get(comp, 1));
	comp->next->nr[0x83] |= 1;
	comp->next->pe3 = 0x82;
	int div = 0x020000 + (2 << 13) + 0x123;
	comp->mem->ramData[div] = 0x5a;
	int vd = comp_peek(comp, 0x2123, MEM_VIEW_READ, &xa);
	printf("view div:  %02X @ %06X\n", vd, xa.abs);
	comp->brkRamMap[div] = MEM_BRK_FETCH;
	printf("view break: %02X @ %06X\n", getBrk(comp, 0x2123) & MEM_BRK_ANY, xa.abs);
	comp->brkRamMap[div] = 0;
	comp_poke(comp, 0x2123, 0x3c, 0);
	printf("view write: %02X\n", comp->mem->ramData[div]);
	compReset(comp, RES_DEFAULT);
	for (int i = 0; i < 200000; i++) compExec(comp);
	printf("next: inta %s, nextreg %s\n", comp->cpu->xinta ? "set" : "none", comp->cpu->xnextreg ? "set" : "none");
	compSetHardware(comp, "ZX48");
	compSetBaseFrq(comp, 3.5);
	compReset(comp, RES_DEFAULT);
	printf("48k:  inta %s, nextreg %s\n", comp->cpu->xinta ? "set" : "none", comp->cpu->xnextreg ? "set" : "none");
	int debt = 0;
	for (int i = 0; i < 400000; i++) {
		compExec(comp);
		if (comp->waitDebt) debt = 1;
	}
	printf("48k:  wait debt %s\n", debt ? "seen" : "none");
	compDestroy(comp);
	return 0;
}

// --reset: a reset from 28 MHz - the RESET button's short press (F4 on the ps/2
// keyboard), NR 02 b0, Machine > Reset, NR 02 b1 - leaves the CPU at 3.5 MHz with its
// T-states on the dot starts of odd hc_ula, where 48K contention reads them, as at
// power-on (zxnext.vhd 5785-5830); each case runs a contended loop and checks every
// instruction boundary
enum {RS_POWER = 0, RS_F4, RS_NR02, RS_MENU, RS_HARD};
static const char* rsNames[] = {"power", "F4", "NR 02 b0", "Machine > Reset", "NR 02 b1 (hard)"};

static void reset_poke(Computer* comp, int adr, const unsigned char* b, int n) {
	for (int i = 0; i < n; i++) comp_poke(comp, adr + i, b[i], 0);
}

static int reset_case(int kind, int delay) {
	// 8000: 48K timing, 28 MHz, a while at it, then NR 02 (its cases) or a loop
	static const unsigned char fast[] = {
		0xf3,				// di
		0xed, 0x91, 0x03, 0x90,		// nextreg 3,#90: 48K timing
		0xed, 0x91, 0x07, 0x03,		// nextreg 7,3: 28 MHz
		0x06, 0x00, 0x10, 0xfe,		// ld b,0: djnz $
		0x06, 0x37, 0x10, 0xfe,		// ld b,#37: djnz $ (the phase the reset lands on)
		0x18, 0x04,			// jr +4 (the NR 02 cases: nop nop over it)
		0xed, 0x91, 0x02, 0x01,		// nextreg 2,1
		0x18, 0xfe			// jr $
	};
	// 8100: the contended loop at 3.5 MHz
	static const unsigned char slow[] = {
		0xf3,				// di
		0x3a, 0x00, 0x40,		// ld a,(#4000)
		0x18, 0xfb			// jr -5
	};
	Computer* comp = compCreate();
	compSetHardware(comp, "ZXNext");
	compSetBaseFrq(comp, 3.5);
	compReset(comp, RES_DEFAULT);
	comp->flgBOOT = 0;
	comp->next->configMode = 0;
	reset_poke(comp, 0x8000, fast, sizeof(fast));
	reset_poke(comp, 0x8100, slow, sizeof(slow));
	comp_poke(comp, 0x800e, 0x30 + delay, 0);
	if ((kind == RS_NR02) || (kind == RS_HARD)) comp_poke(comp, 0x8011, 0, 0), comp_poke(comp, 0x8012, 0, 0);
	if (kind == RS_HARD) comp_poke(comp, 0x8016, 2, 0);		// nextreg 2,2: the hard reset
	if (kind != RS_POWER) {
		comp->cpu->regPC = 0x8000;
		// to the jr $ - or, NR 02's cases, into the ROM the reset starts
		for (int i = 0; (i < 200000) && (comp->cpu->regPC != 0x8017) && (comp->cpu->regPC >= 0x4000); i++)
			compExec(comp);
		switch (kind) {
			case RS_F4: {
				keyEntry k;
				memset(&k, 0, sizeof(k));
				k.atCode = 0x0c;	// F4's set 2 code
				comp->hw->keyp(comp, &k);
				comp->hw->keyr(comp, &k);
				break;
			}
			case RS_MENU:
				compReset(comp, RES_DEFAULT);
				break;
		}
	}
	comp->cpu->regPC = 0x8100;
	int bad = 0, n = 0;
	for (int i = 0; i < 20000; i++) {
		compExec(comp);
		if (i < 2) continue;		// a speed change waits for a bus cycle
		int hc, vc, cvc;
		next_counters(comp, &hc, &vc, &cvc);
		// (the frame after a power-on flips hc_ula's parity at its first restart, and the
		// next bus cycle takes the dot back, nx_relock: the boundary between is not one)
		if (comp->next->powerFrame) continue;
		if (!(hc & 1)) bad++;
		n++;
	}
	int ok = !bad && (comp->next->speed == 0) && (comp->hwMul == 1.0);
	if (!ok) fprintf(stderr, "%s, delay %d: %d of %d boundaries on even hc_ula, speed %d, x%g\n", rsNames[kind], delay, bad, n, comp->next->speed, comp->hwMul);
	compDestroy(comp);
	return ok;
}

static int reset_check(void) {
	xhost_time_fixed = 1759665600;
	printf("== reset speed\n");
	int all = 1;
	for (int k = RS_POWER; k <= RS_HARD; k++) {
		int ok = 1;
		for (int d = 0; d < 8; d++) ok &= reset_case(k, d);	// eight phases the reset lands on
		printf("%s: %s\n", rsNames[k], ok ? "ok" : "FAIL");
		all &= ok;
	}
	return all ? 0 : 1;
}

int debugger_check(void);

int main(int argc, char** argv) {
	if ((argc == 2) && !strcmp(argv[1], "--reset")) return reset_check();
	if ((argc == 2) && !strcmp(argv[1], "--switch")) return switch_check();
	if ((argc == 2) && !strcmp(argv[1], "--debugger")) return debugger_check();
	if (argc < 3) {
		fprintf(stderr, "usage: zxnext-bench sram.bin frames [dump_addr dump_len] [outdir]\n");
		return 2;
	}
	int frames = atoi(argv[2]);
	int dadr = 0x040000;
	int dlen = 8192;
	const char* out = ".";
	if (argc > 4) {
		dadr = (int)strtol(argv[3], NULL, 0);
		dlen = (int)strtol(argv[4], NULL, 0);
	}
	if (argc > 5) out = argv[5];
	xhost_time_fixed = 1759665600;

	Computer* comp = compCreate();
	comp->vid->drawAll = 1;		// the frames are compared whole
	if (!compSetHardware(comp, "ZXNext")) {
		fprintf(stderr, "no ZXNext core\n");
		return 1;
	}
	compSetBaseFrq(comp, 3.5);
	int fw = !strncmp(argv[1], "fw:", 3);
	FILE* f;
	if (fw) {
		// the machine's ROM banks: 0-3, 4 (divMMC), 5 (Multiface)
		static const int at[3] = {0, 0x10000, 0x14000};
		char list[4096];
		snprintf(list, sizeof(list), "%s", argv[1] + 3);
		char* s = list;
		memset(comp->mem->romData, 0xff, MEM_512K);
		for (int i = 0; (i < 3) && s && *s; i++) {
			char* e = strchr(s, ',');
			if (e) *e++ = 0;
			f = fopen(s, "rb");
			if (!f) {
				fprintf(stderr, "can't open %s\n", s);
				return 1;
			}
			size_t n = fread(comp->mem->romData + at[i], 1, (i == 0) ? 0x10000 : 0x2000, f);
			fclose(f);
			(void)n;
			s = e;
		}
		comp_power_on(comp);
	}
	compReset(comp, RES_DEFAULT);
	bytesPerLine = comp->vid->full.x * 8;
	bufSize = bytesPerLine * comp->vid->full.y;
	comp_kbd_release(comp);		// no key down (the GUI does it when it starts)

	if (getenv("NXSDDIR")) {
		// NXSDDIR=folder: the card a folder makes, as the GUI serves one (breadth first)
		vFat* vf = vfat_create();
		static char qpath[8192][512];
		static int qidx[8192];
		int qh = 0, qt = 0;
		snprintf(qpath[qt], 512, "%s", getenv("NXSDDIR"));
		vfat_set_root(vf, getenv("NXSDDIR"), NULL);
		qidx[qt++] = vfat_add(vf, -1, "", 0, 0, 1);
		if (getenv("NXSDOVL")) {
			// NXSDOVL=folder: in the card's root as NXSDSUB (default "game")
			snprintf(qpath[qt], 512, "%s", getenv("NXSDOVL"));
			qidx[qt] = vfat_add(vf, qidx[0], getenv("NXSDSUB") ? getenv("NXSDSUB") : "game", 0, 0, 1);
			vfat_pin(vf, qidx[qt++], getenv("NXSDOVL"));
		}
		while ((qh < qt) && (qt < 8192)) {
			DIR* d = opendir(qpath[qh]);
			struct dirent* e;
			while (d && (e = readdir(d))) {
				if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
				char p[512];
				snprintf(p, sizeof(p), "%s/%s", qpath[qh], e->d_name);
				struct stat st;
				if (stat(p, &st)) continue;
				if (S_ISDIR(st.st_mode)) {
					int i = vfat_add(vf, qidx[qh], e->d_name, 0, (unsigned)st.st_mtime, 1);
					if ((i >= 0) && (qt < 8192)) {
						snprintf(qpath[qt], 512, "%s", p);
						qidx[qt++] = i;
					}
				} else {
					vfat_add(vf, qidx[qh], e->d_name, (unsigned)st.st_size, (unsigned)st.st_mtime, 0);
				}
			}
			if (d) closedir(d);
			qh++;
		}
		if (getenv("NXSDADD")) {
			// NXSDADD=file: one more file in the card's root, as NXSDNAME (8.3)
			struct stat st;
			if (!stat(getenv("NXSDADD"), &st))
				vfat_pin(vf, vfat_add(vf, qidx[0], getenv("NXSDNAME") ? getenv("NXSDNAME") : "PROG.NEX", (unsigned)st.st_size, (unsigned)st.st_mtime, 0), getenv("NXSDADD"));
		}
		vfat_build(vf, 0);
		sdcSetFolder(comp->sdc, getenv("NXSDDIR"), vf);
	}
	if (getenv("NXSD")) {
		// NXSD=image: the SD card in SD0, written to never
		sdcSetImage(comp->sdc, getenv("NXSD"));
		sdcSetLock(comp->sdc, 1);
	}
	if (!fw) {
		comp->next->busRomcs = 1;
		next_reset_open = 1;
		next_ear_tied = 1;
		f = fopen(argv[1], "rb");
		if (!f) {
			fprintf(stderr, "can't open %s\n", argv[1]);
			return 1;
		}
		size_t n = fread(comp->mem->ramData, 1, MEM_2M, f);
		fclose(f);
		(void)n;
		// pages 0A, 0B, 0E: the image is what the SRAM holds; the CPU's BRAM copies start empty
		static const int bram[3] = {0x0a, 0x0b, 0x0e};
		for (int i = 0; i < 3; i++) {
			int a = 0x040000 + (bram[i] << 13);
			memcpy(comp->next->shadow + ((bram[i] == 0x0e) ? 0x4000 : ((bram[i] - 0x0a) << 13)), comp->mem->ramData + a, 0x2000);
			memset(comp->mem->ramData + a, 0, 0x2000);
		}
	}

	char path[4096];
	snprintf(path, sizeof(path), "%s/probe.txt", out);
	fprobe = fopen(path, "w");
	snprintf(path, sizeof(path), "%s/nextreg.txt", out);
	fnr = fopen(path, "w");
	snprintf(path, sizeof(path), "%s/audio.txt", out);
	faudio = fopen(path, "w");
	if (!fprobe || !fnr) {
		fprintf(stderr, "can't write to %s\n", out);
		return 1;
	}
	next_log_out = log_out;
	if (getenv("NXUART")) {
		FILE* fu = fopen(getenv("NXUART"), "r");
		long long o;
		int v;
		while (fu && (uartN < 4096) && (fscanf(fu, "%lld %d", &o, &v) == 2)) {
			uartOff[uartN] = o;
			uartLv[uartN++] = v;
		}
		if (fu) fclose(fu);
	}
	next_log_nr = log_nr;

	// TRACE=n: where each of the first n instructions starts
	int trace = getenv("TRACE") ? atoi(getenv("TRACE")) : 0;
	int swaps = bufSwaps;
	int rows = comp->vid->vend.y;		// the raster the picture under way started with
	int cols = comp->vid->vend.x * 2;
	// NXKEYS="f:keys f:keys ...": from frame f the keys are held KEYHOLD frames (zx keyTab
	// letters, E enter, C caps, S symbol); NXSTOPPC=hex: the state is dumped there, after NXSTOPF
	char keys[4096] = "";
	if (getenv("NXKEYS")) snprintf(keys, sizeof(keys), "%s", getenv("NXKEYS"));
	char* kp = keys;
	keyEntry kent;
	int kdown = -1;
	int khold = getenv("KEYHOLD") ? atoi(getenv("KEYHOLD")) : 3;
	int stoppc = getenv("NXSTOPPC") ? (int)strtol(getenv("NXSTOPPC"), NULL, 16) : -1;
	int stopf = getenv("NXSTOPF") ? atoi(getenv("NXSTOPF")) : 0;
	int nmiat = getenv("NXNMIAT") ? atoi(getenv("NXNMIAT")) : -1;
	if (getenv("NXDUMPNOW")) {
		dump_state(comp, out);
		return 0;
	}
	while (!done && (comp->next->vsyncCount < frames)) {
		if ((stoppc >= 0) && (cpu_get_pc(comp->cpu) == stoppc) && (comp->next->vsyncCount >= stopf)) {
			dump_state(comp, out);
			printf("stop at %04X, frame %d\n", stoppc, comp->next->vsyncCount);
			break;
		}
		// NXNMIAT=f: the NMI key (the M1 button) at frame f
		if (nmiat >= 0 && comp->next->vsyncCount >= nmiat) {
			comp->hw->irq(comp, IRQ_NMI);
			nmiat = -1;
		}
		if ((kdown >= 0) && (comp->next->vsyncCount >= kdown + khold)) {
			kbd_release(comp->keyb, &kent);
			kdown = -1;
		}
		while (*kp == ' ') kp++;
		if ((kdown < 0) && *kp && (comp->next->vsyncCount >= atoi(kp))) {
			char* c = strchr(kp, ':');
			memset(&kent, 0, sizeof(kent));
			int n = 0;
			c = c ? c + 1 : kp;
			while (*c && (*c != ' ') && (n < 7)) {
				char ch = *c++;
				kent.zxKey[n++] = (ch == '_') ? ' ' : ch;	// _ is the space key
			}
			kbd_press(comp->keyb, &kent);
			kdown = comp->next->vsyncCount;
			kp = c;
		}
		if ((trace > 0) && getenv("TRACEAT") && (comp->next->vsyncCount < atoi(getenv("TRACEAT")))) {
			// TRACEAT=f: the n instructions from frame f on
		} else if (trace > 0) {
			int hc, vc, cvc;
			next_counters(comp, &hc, &vc, &cvc);
			printf("TRACE pc %04X t %lld hc %d vc %d af %04X bc %04X de %04X hl %04X sp %04X iff %d\n", cpu_get_pc(comp->cpu), comp->tickTotal, hc, vc,
				comp->cpu->regAF, comp->cpu->regBC, comp->cpu->regDE, comp->cpu->regHL, comp->cpu->regSP, comp->cpu->flgIFF1);
			trace--;
		}
		compExec(comp);
		// NXFRAMEFROM=n: only the frames from n on are written
		if ((bufSwaps != swaps) && !getenv("NXNOFRAMES")) {
			swaps = bufSwaps;
			if (comp->next->vsyncCount > (getenv("NXFRAMEFROM") ? atoi(getenv("NXFRAMEFROM")) : 0))
				dump_frame(comp, out, comp->next->vsyncCount - 1, rows, cols);
			rows = comp->vid->vend.y;
			cols = comp->vid->vend.x * 2;
		}
	}

	fclose(fprobe);
	fclose(fnr);
	if (faudio) fclose(faudio);
	snprintf(path, sizeof(path), "%s/dump.bin", out);
	f = fopen(path, "wb");
	for (int i = 0; i < dlen; i++) {
		int a = (dadr + i) & (MEM_2M - 1);
		int pg = (a - 0x040000) >> 13;
		unsigned char b = comp->mem->ramData[a];
		if ((a >= 0x040000) && ((pg == 0x0a) || (pg == 0x0b) || (pg == 0x0e)))
			b = comp->next->shadow[((pg == 0x0e) ? 0x4000 : ((pg - 0x0a) << 13)) | (a & 0x1fff)];
		fputc(b, f);
	}
	fclose(f);
	printf("%s: frame %d, %s\n", base_name(argv[1]), comp->next->vsyncCount, done ? "ended by the program" : "frame limit");
	if (fw) printf("pc %04X\n", cpu_get_pc(comp->cpu));
	if (comp->next->v.late) printf("register changes drawn late: %d\n", comp->next->v.late);
	return 0;
}
