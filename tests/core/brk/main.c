// Headless breakpoint bench: a ZX Spectrum 48K whose breakpoints are checked
// against the bus cycles of the Z80 CPU User Manual. Read is a memory read
// without M1 (operands, DD CB d and op, data, stack, the IM 2 vector); Fetch is
// an M1 the CPU runs (the opcode and its prefixes), once before the
// instruction; Interrupt is an interrupt the CPU takes. The steps run the way
// the app's thread runs them: a break raised before the CPU ran is stepped over
// with the checks off. Every access a step makes is a hit of its own, on each
// map that watches it.
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
	int hits;		// steps that raised a break
	int pre;		// ...of them before the step ran
	int adr;		// the last hit's cpu address
	int pc;			// ...and the PC it left
	int kind;		// ...and its type
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
	const xBrkHit* last = &comp->brkhit[comp->brkn - 1];
	int pre = comp->flgBRKPRE;
	h->hits++;
	h->pre += pre;
	h->adr = last->adr;
	h->pc = comp->cpu->regPC;
	h->kind = last->t;
	comp_brk_clear(comp);
	if (pre) *skip = 1;
}

// the step's hit of this type and cpu address, NULL when there is none
static const xBrkHit* hit_of(Computer* comp, int t, int adr) {
	for (int i = 0; i < comp->brkn; i++)
		if ((comp->brkhit[i].t == t) && (comp->brkhit[i].adr == adr)) return &comp->brkhit[i];
	return NULL;
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
		ok &= !cpu_int_ready(cpu);
		cpu->intrq |= Z80_NMI;
		ok &= cpu_int_ready(cpu);		// NMI whatever IFF1 says
		check("taken: NMI, or INT with IFF1, past EI, sampled; IFF1 edits follow", ok && on);
		compDestroy(comp);
	}
	{
		// an NMI handler after EI (IFF1 off, IFF2 on) whose registers the debugger
		// writes back: RETN puts IFF2 into IFF1 and INT comes in again
		Computer* comp = machine();
		CPU* cpu = comp->cpu;
		comp_poke(comp, 0x8060, 0xed, 0);	// RETN
		comp_poke(comp, 0x8061, 0x45, 0);
		cpu->regPC = 0x8060;
		cpu->regSP = 0x8ffe;
		comp_poke(comp, 0x8ffe, 0x20, 0);	// back to #8020
		comp_poke(comp, 0x8fff, 0x80, 0);
		cpu->inten |= Z80_INT;
		cpu->flgIFF1 = 0;
		cpu->flgIFF2 = 1;
		cpu_set_reg(cpu, "IFF1", 0);
		compExec(comp);
		cpu->intrq |= Z80_INT;
		cpu->flgACK = 1;
		check("an IFF1 edit, then RETN: INT is taken", (cpu->regPC == 0x8020) && cpu->flgIFF1 && cpu_int_ready(cpu));
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
	{
		// IM 2 counts as a CALL does, to the cap where Step Out gives up
		Computer* comp = machine();
		CPU* cpu = comp->cpu;
		cpu->regPC = 0x8020;
		cpu_set_reg(cpu, "IFF1", 1);
		cpu->intrq |= Z80_INT;
		cpu->flgACK = 1;
		cpu->flgRetBRK = 1;
		cpu->regCallCnt = 65000;
		compExec(comp);
		check("IM 2 counts to the cap", (cpu->regPC == 0x8383) && !cpu->flgRetBRK && (cpu->regCallCnt == 65000));
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
		check("Fetch on TR-DOS #3D2F from BASIC", comp->flgBRK && (comp->brkn == 1) && (comp->brkhit[0].t == BRK_MEMROM)
			&& (comp->brkhit[0].a == 0x7d2f) && !comp->flgDOS);
		compDestroy(comp);
	}
	{
		// the heat map counts the TR-DOS byte that runs, as the Fetch point sees it
		Computer* comp = machine();
		difSetHW(comp->dif, DIF_BDI);
		comp->mem->romData[0x7d2f] = 0x00;		// TR-DOS: NOP
		comp->hw->mapMem(comp);
		comp_heat_sync(comp);
		comp->flgHEAT = 1;
		comp->cpu->regPC = 0x3d2f;
		compExec(comp);
		check("heat of a #3Dxx fetch in TR-DOS", comp->flgDOS && (comp->cpu->regPC == 0x3d30)
			&& (comp->heatRom.ex[0x7d2f] == 1) && !comp->heatRom.ex[0x3d2f] && !comp->heatRom.rd[0x3d2f]);
		compDestroy(comp);
	}
	{
		// the listing: every row in the map the fetch at PC sees
		Computer* comp = machine();
		difSetHW(comp->dif, DIF_BDI);
		comp->mem->romData[0x3d14] = 0x11;		// BASIC
		comp->mem->romData[0x7d14] = 0x22;		// TR-DOS
		comp->mem->romData[0x0100] = 0x33;
		comp->mem->romData[0x4100] = 0x44;
		comp->hw->mapMem(comp);
		comp->cpu->regPC = 0x3d13;
		ok = (comp_peek(comp, 0x3d14, MEM_VIEW_PC, NULL) == 0x22) && (comp_peek(comp, 0x0100, MEM_VIEW_PC, NULL) == 0x44);
		ok &= (comp_get_xadr(comp, 0x0100, MEM_VIEW_PC).abs == 0x4100) && (comp_peek(comp, 0x0100, MEM_VIEW_READ, NULL) == 0x33);
		ok &= !comp->flgDOS;
		comp->flgDOS = 1;		// in TR-DOS, PC in RAM: its fetch pages TR-DOS out
		comp->hw->mapMem(comp);
		comp->cpu->regPC = 0x8000;
		ok &= (comp_peek(comp, 0x0100, MEM_VIEW_PC, NULL) == 0x33) && (comp_peek(comp, 0x0100, MEM_VIEW_READ, NULL) == 0x44);
		ok &= comp->flgDOS;
		check("listing at PC #3D13 in TR-DOS, at PC in RAM in BASIC", ok);
		compDestroy(comp);
	}
	{
		// a divMMC: the board from #3Dxx at once, after an entry point's fetch only
		Computer* comp = machine();
		div_set_model(comp->div, DIV_MMC, 16);
		div_out(comp->div, 0xe3, 0x00);		// the automapper alone
		comp->div->data[0x0009] = 0x5a;
		comp->div->data[0x0100] = 0x5b;
		comp->mem->romData[0x0009] = 0xa5;
		comp->mem->romData[0x0100] = 0xa6;
		comp->flgDBG = 1;
		comp->cpu->regPC = 0x0008;
		ok = (comp_peek(comp, 0x0009, MEM_VIEW_PC, NULL) == 0xa5);
		div_m1(comp->div, 0x0008);		// RST 8 fetched there
		comp->cpu->regPC = 0x0009;
		ok &= (comp_peek(comp, 0x0009, MEM_VIEW_PC, NULL) == 0x5a);
		div_m1(comp->div, 0x1ff8);		// off for the fetch after #1FF8
		div_m1(comp->div, 0x8000);
		ok &= !comp->div->automap;
		comp->cpu->regPC = 0x3d00;
		ok &= (comp_peek(comp, 0x0100, MEM_VIEW_PC, NULL) == 0x5b) && (comp_peek(comp, 0x0100, MEM_VIEW_READ, NULL) == 0xa6);
		comp->flgDBG = 0;
		check("listing with a divMMC: #3Dxx at once, RST 8 after its fetch", ok);
		compDestroy(comp);
	}

	printf("== hits\n");
	{
		// PUSH HL writes #8FFF then #8FFE: both points get theirs
		Computer* comp = machine();
		comp_poke(comp, 0x8060, 0xe5, 0);
		comp->cpu->regPC = 0x8060;
		comp->cpu->regHL = 0x1234;
		comp->brkAdrMap[0x8fff] = MEM_BRK_WR;
		comp->brkAdrMap[0x8ffe] = MEM_BRK_WR;
		comp->flgBRKMEM = 1;
		compExec(comp);
		const xBrkHit* hi = hit_of(comp, BRK_CPUADR, 0x8fff);
		const xBrkHit* lo = hit_of(comp, BRK_CPUADR, 0x8ffe);
		check("PUSH HL: both bytes, each with its value", (comp->brkn == 2) && hi && lo
			&& (hi->kind == MEM_BRK_WR) && (hi->val == 0x12) && (lo->val == 0x34)
			&& (comp->brkhit[0].adr == 0x8fff));
		compDestroy(comp);
	}
	{
		// a cpu address point and the cell under it, on one byte
		Computer* comp = machine();
		xAdr cell = comp_get_xadr(comp, 0xa010, MEM_VIEW_READ);
		comp->brkAdrMap[0xa010] = MEM_BRK_RD;
		comp->brkRamMap[cell.abs] = MEM_BRK_RD;
		comp->flgBRKMEM = 1;
		comp->cpu->regPC = 0x800a;		// LD A,(#A010)
		compExec(comp);
		const xBrkHit* cpu = hit_of(comp, BRK_CPUADR, 0xa010);
		const xBrkHit* ram = hit_of(comp, BRK_MEMRAM, 0xa010);
		check("cpu address and cell on one byte: both, the byte read", (comp->brkn == 2) && cpu && ram
			&& (cpu->a == 0xa010) && (ram->a == cell.abs) && (cpu->val == 0x77) && (ram->val == 0x77));
		compDestroy(comp);
	}
	{
		// DD CB: a fetch point on the prefix and one on the CB both
		Computer* comp = machine();
		comp->brkAdrMap[0x8004] = MEM_BRK_FETCH;
		comp->brkAdrMap[0x8005] = MEM_BRK_FETCH;
		comp->flgBRKMEM = 1;
		comp->cpu->regPC = 0x8004;
		compExec(comp);
		check("DD CB: the prefix and the CB, both", comp->flgBRKPRE && (comp->brkn == 2)
			&& hit_of(comp, BRK_CPUADR, 0x8004) && hit_of(comp, BRK_CPUADR, 0x8005));
		compDestroy(comp);
	}
	{
		// run to cursor where a cpu address fetch point stands: the temp point too
		Computer* comp = machine();
		xAdr cell = comp_get_xadr(comp, 0x8008, MEM_VIEW_READ);
		comp->brkAdrMap[0x8008] = MEM_BRK_FETCH;
		comp->brkRamMap[cell.abs] = MEM_BRK_TFETCH;
		comp->flgBRKMEM = 1;
		comp->cpu->regPC = 0x8008;
		compExec(comp);
		check("a temp point under a cpu address fetch point", (comp->brkn == 2) && hit_of(comp, BRK_CPUADR, 0x8008)
			&& hit_of(comp, BRK_TEMP, 0x8008) && !(comp->brkRamMap[cell.abs] & MEM_BRK_TFETCH));
		compDestroy(comp);
	}
	{
		// run to cursor waits for PC: an instruction whose prefix stands before the
		// cursor's byte runs on, a Fetch point there still gets the prefixed fetch
		Computer* comp = machine();
		comp_poke(comp, 0x8060, 0xdd, 0);	// DD
		comp_poke(comp, 0x8061, 0x23, 0);	// INC IX
		comp_poke(comp, 0x8062, 0x00, 0);	// NOP
		xAdr cell = comp_get_xadr(comp, 0x8061, MEM_VIEW_READ);
		comp->brkRamMap[cell.abs] = MEM_BRK_TFETCH;
		comp->flgBRKMEM = 1;
		comp->cpu->regPC = 0x8060;
		compExec(comp);
		ok = !comp->flgBRK && (comp->cpu->regPC == 0x8062) && (comp->brkRamMap[cell.abs] & MEM_BRK_TFETCH);
		comp->brkRamMap[cell.abs] = MEM_BRK_FETCH;
		comp->cpu->regPC = 0x8060;
		compExec(comp);
		ok &= comp->flgBRKPRE && (hit_of(comp, BRK_MEMRAM, 0x8061) != NULL);
		check("a temp point waits for PC, not a prefix before it", ok);
		compDestroy(comp);
	}
	{
		// a recording played back reads the port all the same
		Computer* comp = machine();
		comp_poke(comp, 0x8060, 0xdb, 0);	// IN A,(#FE)
		comp_poke(comp, 0x8061, 0xfe, 0);
		comp->cpu->regPC = 0x8060;
		comp->cpu->regA = 0x7f;
		comp->brkIOMap[0x7ffe] = MEM_BRK_RD;
		comp->rzx.play = 1;
		comp->rzx.frm.fetches = 100;
		comp->rzx.frm.size = 1;
		comp->rzx.frm.pos = 0;
		comp->rzx.frm.data[0] = 0x5a;
		compExec(comp);
		const xBrkHit* io = hit_of(comp, BRK_IOPORT, 0x7ffe);
		check("IN under RZX playback", (comp->cpu->regA == 0x5a) && io && (io->kind == MEM_BRK_RD) && (io->val == 0x5a));
		comp->rzx.play = 0;
		compDestroy(comp);
	}
	return failures ? 1 : 0;
}
