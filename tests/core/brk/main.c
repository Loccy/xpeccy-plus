// Headless breakpoint bench: a ZX Spectrum 48K whose breakpoints are checked
// against the bus cycles of the Z80 CPU User Manual. Read is a memory read
// without M1 (operands, DD CB d and op, data, stack, the IM 2 vector); Fetch is
// an M1 the CPU runs (the opcode and its prefixes), once before the
// instruction; Interrupt is an interrupt the CPU takes. The steps run the way
// the app's thread runs them: a break raised before the CPU ran is stepped over
// with the checks off.
//
//	brk-bench

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spectrum.h"
#include "cpu/Z80/z80.h"

static int failures;

static void check(const char* name, int ok) {
	printf("%s: %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) failures++;
}

// The program (and the snapshot made of it for the other emulators' runs):
// IM 2 with I = #81, the table at #8100-#8200 all #83, the handler at #8383.
static const unsigned char prog[] = {
	0xdd, 0x21, 0x00, 0xa0,		// 8000 LD IX,#A000
	0xdd, 0xcb, 0x01, 0x6e,		// 8004 BIT 5,(IX+1): 8004 8005 M1, 8006 d, 8007 op, A001 data
	0x3e, 0x55,			// 8008 LD A,#55: 8009 a read
	0x3a, 0x10, 0xa0,		// 800A LD A,(#A010): 800B 800C reads, A010 data
	0xed, 0x44,			// 800D NEG: 800D 800E M1
	0xfb,				// 800F EI
	0x76,				// 8010 HALT: 8011 fetched while halted, nothing run
	0x00,				// 8011 NOP
	0x18, 0xf0,			// 8012 JR #8004
};

static const unsigned char loop[] = {
	0x03,				// 8020 INC BC
	0x18, 0xfd,			// 8021 JR #8020
};

static const unsigned char sub[] = {
	0x10, 0xfe,			// 8040 DJNZ #8040
	0xc9,				// 8042 RET
};

static Computer* machine(void) {
	Computer* comp = compCreate();
	if (!compSetHardware(comp, "ZX48")) {
		fprintf(stderr, "no ZX48 core\n");
		exit(1);
	}
	vLayout lay = {{448, 312}, {64, 56}, {64, 16}, {256, 192}, {120, 8}, 64};
	comp_set_layout(comp, &lay);
	bytesPerLine = comp->vid->full.x * 8;
	memSetSize(comp->mem, MEM_64K, MEM_32K);
	compSetBaseFrq(comp, 3.5);
	comp->resbank = RES_48;
	compReset(comp, RES_DEFAULT);
	comp->flgBOOT = 0;
	for (int i = 0; i < (int)sizeof(prog); i++) comp_poke(comp, 0x8000 + i, prog[i], 0);
	for (int i = 0; i < (int)sizeof(loop); i++) comp_poke(comp, 0x8020 + i, loop[i], 0);
	for (int i = 0; i < (int)sizeof(sub); i++) comp_poke(comp, 0x8040 + i, sub[i], 0);
	for (int i = 0x8100; i <= 0x8200; i++) comp_poke(comp, i, 0x83, 0);
	comp_poke(comp, 0x8383, 0xfb, 0);	// EI
	comp_poke(comp, 0x8384, 0xc9, 0);	// RET
	comp_poke(comp, 0xa001, 0x20, 0);
	comp_poke(comp, 0xa010, 0x77, 0);
	comp->mem->romData[0x38] = 0xfb;	// IM 1: EI; RET
	comp->mem->romData[0x39] = 0xc9;
	CPU* cpu = comp->cpu;
	cpu->regPC = 0x8000;
	cpu->regSP = 0x9000;
	cpu->regI = 0x81;
	cpu->regIM = 2;
	cpu->flgIFF1 = cpu->flgIFF2 = 0;
	cpu->inten = Z80_NMI;
	return comp;
}

typedef struct {
	int hits;		// breaks raised
	int pre;		// ...of them before the step ran
	int adr;		// the last one's brkev.adr
	int pc;			// ...and the PC it left
	int kind;		// ...and its brkt
} xHits;

// one step as the app's thread takes it
static void step(Computer* comp, xHits* h, int* skip) {
	if (*skip) {
		comp->flgDBG = 1;
		compExec(comp);
		comp->flgDBG = 0;
		*skip = 0;
	} else {
		compExec(comp);
	}
	if (!comp->flgBRK) return;
	h->hits++;
	h->pre += comp->flgBRKPRE;
	h->adr = comp->brkev.adr;
	h->pc = comp->cpu->regPC;
	h->kind = comp->brkt;
	comp->flgBRK = 0;
	if (comp->flgBRKPRE) *skip = 1;
}

// the program from #8000 until it has taken ints interrupts and stands at #8012
// (each pass through #8004..#8011 ends in one)
static xHits run_prog(Computer* comp, int ints) {
	xHits h = {0, 0, -1, -1, 0};
	int skip = 0;
	int base = comp->intTaken;
	for (long n = 0; n < 10000000; n++) {
		step(comp, &h, &skip);
		if ((comp->intTaken - base >= ints) && (comp->cpu->regPC == 0x8012) && !skip) break;
	}
	return h;
}

static xHits prog_bp(int adr, int type) {
	Computer* comp = machine();
	comp->brkAdrMap[adr] = type;
	comp->flgBRKMEM = 1;
	xHits h = run_prog(comp, 5);
	compDestroy(comp);
	return h;
}

static int read_hits(int adr) {
	return prog_bp(adr, MEM_BRK_RD).hits;
}

// INC BC; JR for that many frames, interrupts on (IM 2) or off
static xHits run_loop(Computer* comp, int frames, int ei, int* ints) {
	xHits h = {0, 0, -1, -1, 0};
	int skip = 0;
	comp->cpu->regPC = 0x8020;
	comp->cpu->regBC = 0;
	cpu_set_reg(comp->cpu, "IFF1", ei);
	comp->cpu->flgIFF2 = ei;
	int base = comp->intTaken;
	long long end = comp->tickTotal + (long long)frames * 69888;
	while ((comp->tickTotal < end) || skip)
		step(comp, &h, &skip);
	if (ints) *ints = comp->intTaken - base;
	return h;
}

int main(void) {
	printf("== read\n");
	int ok = 1;
	const int reads[] = {0x8006, 0x8007, 0x8009, 0x800b, 0x800c, 0xa001, 0xa010, 0x81ff, 0x8200, 0x8ffe, 0x8fff};
	for (int i = 0; i < (int)(sizeof(reads) / sizeof(reads[0])); i++) {
		int n = read_hits(reads[i]);
		if (n != 5) fprintf(stderr, "read #%04X: %d\n", reads[i], n);
		ok &= (n == 5);
	}
	check("operands, DD CB d and op, data, IM 2 vector, stack are reads", ok);
	ok = 1;
	const int m1[] = {0x8004, 0x8005, 0x800d, 0x800e, 0x8010, 0x8011, 0x8012};
	for (int i = 0; i < (int)(sizeof(m1) / sizeof(m1[0])); i++) {
		int n = read_hits(m1[i]);
		if (n) fprintf(stderr, "read #%04X: %d\n", m1[i], n);
		ok &= !n;
	}
	check("opcode, prefix and HALT fetches are no reads", ok);
	check("LD IX,nn operands read once", (read_hits(0x8002) == 1) && (read_hits(0x8003) == 1));
	xHits h = prog_bp(0xa001, MEM_BRK_RD);
	check("a read stops after its instruction", !h.pre && (h.adr == 0xa001) && (h.pc == 0x8008));

	printf("== fetch\n");
	h = prog_bp(0x8004, MEM_BRK_FETCH);
	check("instruction start", (h.hits == 5) && (h.pre == 5) && (h.adr == 0x8004) && (h.pc == 0x8004));
	h = prog_bp(0x8005, MEM_BRK_FETCH);
	check("DD CB: the CB is a fetch, before the instruction", (h.hits == 5) && (h.pre == 5) && (h.adr == 0x8005) && (h.pc == 0x8004));
	check("DD CB: d and op are no fetches", !prog_bp(0x8006, MEM_BRK_FETCH).hits && !prog_bp(0x8007, MEM_BRK_FETCH).hits);
	h = prog_bp(0x800e, MEM_BRK_FETCH);
	check("ED: the opcode after it is a fetch", (h.hits == 5) && (h.adr == 0x800e) && (h.pc == 0x800d));
	check("operands are no fetches", !prog_bp(0x8009, MEM_BRK_FETCH).hits && !prog_bp(0x8002, MEM_BRK_FETCH).hits);
	h = prog_bp(0x8010, MEM_BRK_FETCH);
	check("HALT once, not while halted", (h.hits == 5) && (h.pc == 0x8010));
	h = prog_bp(0x8011, MEM_BRK_FETCH);
	check("the byte after HALT when it runs only", h.hits == 5);
	{
		Computer* comp = machine();
		int ints;
		comp->brkAdrMap[0x8020] = MEM_BRK_FETCH;
		comp->flgBRKMEM = 1;
		h = run_loop(comp, 10, 1, &ints);
		check("no fetch where an interrupt is taken instead", (ints >= 9) && (h.hits == comp->cpu->regBC));
		compDestroy(comp);
	}

	printf("== interrupt\n");
	{
		Computer* comp = machine();
		comp->flgIBRK = 1;
		xHits hi = run_prog(comp, 5);
		check("IRQ once per interrupt taken, halted", (hi.hits == 5) && (hi.pre == 5) && (hi.kind == BRK_IRQ) && (hi.pc == 0x8010));
		compDestroy(comp);
	}
	{
		Computer* comp = machine();
		int ints, req = 0;
		comp->flgIBRK = 1;
		comp->brkAdrMap[0x8020] = MEM_BRK_FETCH;
		comp->flgBRKMEM = 1;
		// the IRQ hits and the fetch hits apart: both are breaks
		xHits hb = run_loop(comp, 10, 1, &ints);
		check("IRQ and fetch on the same step: both, once", (ints >= 9) && (hb.hits == ints + comp->cpu->regBC));
		compDestroy(comp);
		comp = machine();
		comp->flgIBRK = 1;
		xHits hd = {0, 0, -1, -1, 0};
		int skip = 0;
		comp->cpu->regPC = 0x8020;
		long long end = comp->tickTotal + 5 * 69888;
		while (comp->tickTotal < end) {
			step(comp, &hd, &skip);
			req |= comp->cpu->intrq & Z80_INT;
		}
		check("no IRQ under DI, the request there", !hd.hits && req && !comp->intTaken);
		compDestroy(comp);
	}
	{
		Computer* comp = machine();
		CPU* cpu = comp->cpu;
		cpu_set_reg(cpu, "IFF1", 1);
		int on = (cpu->inten & Z80_INT) != 0;
		cpu->intrq = Z80_INT;
		cpu->flgACK = 1;
		cpu->flgNOINT = 1;
		ok = !cpu_int_ready(cpu);		// EI's one instruction
		cpu->flgNOINT = 0;
		ok &= cpu_int_ready(cpu);
		cpu->flgACK = 0;
		ok &= !cpu_int_ready(cpu);		// not sampled
		cpu->flgACK = 1;
		cpu_set_reg(cpu, "IFF1", 0);
		ok &= !cpu_int_ready(cpu) && !(cpu->inten & Z80_INT);
		cpu->intrq |= Z80_NMI;
		ok &= cpu_int_ready(cpu);		// NMI whatever IFF1 says
		check("taken: NMI, or INT with IFF1, past EI, sampled; IFF1 edits follow", ok && on);
		compDestroy(comp);
	}

	printf("== step out\n");
	{
		// Step Out inside a call: an IM 1 interrupt there is one call, its RET one return
		Computer* comp = machine();
		CPU* cpu = comp->cpu;
		cpu->regIM = 1;
		cpu->regSP = 0x9000;
		cpu->regSP -= 2;
		comp_poke(comp, cpu->regSP, 0x33, 0);		// returns to #8033
		comp_poke(comp, cpu->regSP + 1, 0x80, 0);
		cpu->regPC = 0x8040;
		cpu->regB = 0;
		cpu_set_reg(cpu, "IFF1", 1);
		cpu->intrq |= Z80_INT;
		cpu->flgACK = 1;
		cpu->flgRetBRK = 1;
		cpu->regCallCnt = 0;
		int ints = comp->intTaken;
		for (long n = 0; (n < 100000) && !comp->flgBRK; n++) compExec(comp);
		check("IM 1 counts once", comp->flgBRK && (cpu->regPC == 0x8033) && (comp->intTaken > ints));
		compDestroy(comp);
	}

	printf("== condition\n");
	{
		Computer* comp = machine();
		comp->flgCOND = 1;
		comp->cpu->regPC = 0x8008;
		comp_brk_newstep(comp);
		compExec(comp);
		int rd1 = comp->brkev.rd;
		comp_brk_newstep(comp);
		compExec(comp);			// LD A,(#A010)
		int rd2 = comp->brkev.rd;
		comp_brk_newstep(comp);
		compExec(comp);			// NEG
		int rd3 = comp->brkev.rd;
		check("RD: operands and data, no fetches", (rd1 == 0x8009) && (rd2 == 0xa010) && (rd3 == -1));
		compDestroy(comp);
	}

	printf("== tr-dos\n");
	{
		Computer* comp = machine();
		difSetHW(comp->dif, DIF_BDI);
		comp->mem->romData[0x3d2f] = 0x11;		// BASIC
		comp->mem->romData[0x7d2f] = 0x22;		// TR-DOS
		comp->hw->mapMem(comp);
		xAdr xa;
		ok = (comp_peek(comp, 0x3d2f, MEM_VIEW_FETCH, &xa) == 0x22) && (xa.type == MEM_ROM) && (xa.abs == 0x7d2f);
		ok &= (comp_peek(comp, 0x3d2f, MEM_VIEW_READ, NULL) == 0x11);
		ok &= !comp->flgDOS && (comp_get_xadr(comp, 0x3d2f, MEM_VIEW_READ).abs == 0x3d2f);
		check("a #3Dxx fetch is seen in TR-DOS, the map left as it was", ok);
		comp->brkRomMap[0x7d2f] = MEM_BRK_FETCH;
		comp->flgBRKMEM = 1;
		comp->cpu->regPC = 0x3d2f;
		compExec(comp);
		check("Fetch on TR-DOS #3D2F from BASIC", comp->flgBRK && (comp->brkt == BRK_MEMROM) && (comp->brka == 0x7d2f) && !comp->flgDOS);
		compDestroy(comp);
	}
	return failures ? 1 : 0;
}
