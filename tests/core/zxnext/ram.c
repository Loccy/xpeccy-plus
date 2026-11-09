// The Next with 1 MB: an issue 2 board without its two expansion chips. SRAM from
// 0x100000 on (RAM pages 0x60-0xDF) has no chip: writes are lost, a read takes what
// that half of the data bus (A0) still holds - the last byte a cpu, DMA or layer 2
// cycle put on it.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spectrum.h"
#include "cpu/Z80/z80.h"
#include "filetypes/filetypes.h"
#include "filetypes/szx.h"

static int failures;

static void check(const char* name, int ok) {
	printf("%s: %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) failures++;
}

#define SRAM_PAGE(p)	(0x40000 + (p) * 0x2000)

static Computer* machine(int size) {
	Computer* comp = compCreate();
	compSetHardware(comp, "ZXNext");
	next_ram_size(comp, size);
	compSetBaseFrq(comp, 3.5);
	compReset(comp, RES_DEFAULT);
	comp->flgBOOT = 0;
	comp->next->configMode = 0;
	comp->next->nr[0x80] = 0;
	comp->next->nr[0x83] = 0;
	comp->next->nr[0x8c] = 0;
	comp->next->p123b = 0;
	return comp;
}

// LD (HL),A then LD A,(HL) at code (a cpu address), page 0x60 at C000
static int write_read(Computer* comp, int code, int pg, int hl) {
	unsigned char* ram = comp->mem->ramData;
	ram[SRAM_PAGE(pg) + (code & 0x1fff)] = 0x77;
	ram[SRAM_PAGE(pg) + (code & 0x1fff) + 1] = 0x7e;
	next_mmu_set(comp, 6, 0x60);
	comp->cpu->regPC = code;
	comp->cpu->regHL = hl;
	comp->cpu->regA = 0xa5;
	compExec(comp);
	compExec(comp);
	return comp->cpu->regA;
}

int ram_check(void) {
	printf("== ram size\n");
	Computer* comp = machine(MEM_1M);
	Computer* big = machine(MEM_2M);
	check("pages: 0x60 on 1 MB, 0xE0 on 2 MB", (comp_cell_pages(comp, CELL_RAM) == 0x60) && (comp_cell_pages(big, CELL_RAM) == 0xe0));
	check("the half past 1 MB filled #FF", (comp->mem->ramData[0x100000] == 0xff) && (comp->mem->ramData[0x1fffff] == 0xff));

	// code in BRAM (bank 5 at 4000) makes no SRAM cycle: the bus keeps the byte written
	int v = write_read(comp, 0x4000, 0x0a, 0xc001);
	check("write lost", comp->mem->ramData[0x100001] == 0xff);
	check("read at once from BRAM code: the byte written", v == 0xa5);

	// code in SRAM: the fetch of LD A,(HL) at 8001 (odd) puts 7E on the odd half
	next_mmu_set(comp, 4, 0x30);
	v = write_read(comp, 0x8000, 0x30, 0xc000);
	check("even byte after an odd fetch: the byte written", v == 0xa5);
	int peek = comp_peek(comp, 0xc000, MEM_VIEW_READ, NULL);
	v = write_read(comp, 0x8000, 0x30, 0xc001);
	check("odd byte after an odd fetch: the opcode", v == 0x7e);
	check("the debugger sees what the cpu reads", (comp_peek(comp, 0xc001, MEM_VIEW_READ, NULL) == 0x7e) && (peek == 0xa5));

	next_mmu_set(big, 4, 0x30);
	v = write_read(big, 0x8000, 0x30, 0xc001);
	check("2 MB: the byte written", (v == 0xa5) && (big->mem->ramData[0x100001] == 0xa5));

	// layer 2 shown from bank 8 (pages 10-15), even bytes 11, odd 22; the cpu reads page
	// 0x60 from BRAM code: in the picture each half carries the last pixel fetched on it
	for (int a = 0; a < 0xc000; a++)
		comp->mem->ramData[SRAM_PAGE(0x10) + a] = (a & 1) ? 0x22 : 0x11;
	comp->next->nr[0x12] = 8;
	comp->next->p123b = 2;
	unsigned char* code = comp->mem->ramData + SRAM_PAGE(0x0a);
	for (int a = 0; a < 0x100; a++) code[a] = 0x7e;		// LD A,(HL) on and on
	next_mmu_set(comp, 6, 0x60);
	comp->cpu->regPC = 0x4000;
	int hc = 0, vc = 0, cvc, low = 0;
	for (int i = 0; i < 200000; i++) {
		next_counters(comp, &hc, &vc, &cvc);
		// the bottom border first: above the first frame's paper vc_ula counts from 0
		if (vc >= 200) low = 1;
		if (low && (vc >= 40) && (vc < 150) && (hc >= 40) && (hc < 200)) break;
		if (comp->cpu->regPC >= 0x40f0) comp->cpu->regPC = 0x4000;
		compExec(comp);
	}
	comp->cpu->regHL = 0xc000;
	compExec(comp);
	int even = comp->cpu->regA;
	comp->cpu->regHL = 0xc001;
	compExec(comp);
	int odd = comp->cpu->regA;
	check("in layer 2's picture: its pixels", (even == 0x11) && (odd == 0x22));
	// layer 2 itself past 1 MB drives nothing: the cpu's byte stays
	comp->next->nr[0x12] = 50;
	v = write_read(comp, 0x4000, 0x0a, 0xc001);
	check("layer 2 past 1 MB: the byte written", v == 0xa5);

	// a snapshot of a 1 MB Next puts a 2 MB one on 1 MB, and back
	szxBuf b;
	int err = szx_build(comp, &b);
	int ok = !err && !b.fail;
	if (ok) ok = (loadSZX_buf(big, b.data, b.len) == ERR_OK) && (big->mem->ramSize == MEM_1M) && (big->mem->ramData[0x100001] == 0xff);
	free(b.data);
	check("a 1 MB snapshot on a 2 MB Next", ok);
	compDestroy(comp);
	comp = machine(MEM_2M);
	err = szx_build(comp, &b);
	ok = !err && !b.fail && (loadSZX_buf(big, b.data, b.len) == ERR_OK) && (big->mem->ramSize == MEM_2M);
	free(b.data);
	check("a 2 MB snapshot on a 1 MB Next", ok);

	compDestroy(comp);
	compDestroy(big);
	return failures ? 1 : 0;
}
