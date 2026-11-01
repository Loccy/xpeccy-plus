#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#include "heatmap.h"
#include "spectrum.h"

static int heat_chunks(int size) {
	return (size + (1 << HEAT_TOT_SHIFT) - 1) >> HEAT_TOT_SHIFT;
}

void heatBankFree(xHeatBank* bank) {
	free(bank->rd);
	free(bank->wr);
	free(bank->ex);
	free(bank->tot);
	bank->rd = NULL;
	bank->wr = NULL;
	bank->ex = NULL;
	bank->tot = NULL;
	bank->size = 0;
}

static void heatBankSync(xHeatBank* bank, int newSize) {
	if (bank->size == newSize)
		return;
	heatBankFree(bank);
	if (newSize > 0) {
		bank->rd = (unsigned int*)calloc(newSize, sizeof(unsigned int));
		bank->wr = (unsigned int*)calloc(newSize, sizeof(unsigned int));
		bank->ex = (unsigned int*)calloc(newSize, sizeof(unsigned int));
		bank->tot = (unsigned long long*)calloc(heat_chunks(newSize) * 3, sizeof(unsigned long long));
		bank->size = newSize;
	}
}

static void heatBankReset(xHeatBank* bank) {
	if (bank->size <= 0) return;
	memset(bank->rd, 0x00, bank->size * sizeof(unsigned int));
	memset(bank->wr, 0x00, bank->size * sizeof(unsigned int));
	memset(bank->ex, 0x00, bank->size * sizeof(unsigned int));
	memset(bank->tot, 0x00, heat_chunks(bank->size) * 3 * sizeof(unsigned long long));
}

static void heatBankHit(xHeatBank* bank, int idx, int kind) {
	if ((idx < 0) || (idx >= bank->size)) return;
	unsigned int* arr;
	switch (kind) {
		case HEAT_RD: arr = bank->rd; break;
		case HEAT_WR: arr = bank->wr; break;
		case HEAT_EX: arr = bank->ex; break;
		default: return;
	}
	if (arr[idx] < UINT_MAX)
		arr[idx]++;
	bank->tot[(idx >> HEAT_TOT_SHIFT) * 3 + kind]++;
}

static int heat_is_next(Computer* comp) {
	return comp->hw && (comp->hw->id == HW_ZXNEXT);
}

// divMMC's EEPROM and SRAM as one bank, the way its data[] holds them. The
// Next keeps its divMMC in the SRAM image, which the RAM bank covers
static int heat_div_size(Computer* comp) {
	if (heat_is_next(comp) || !comp->div || !comp->div->model) return 0;
	return DIV_EE_SIZE + (comp->div->pages << 13);
}

void comp_heat_sync(Computer* comp) {
	heatBankSync(&comp->heatRam, comp->mem->ramMask + 1);
	heatBankSync(&comp->heatRom, comp->mem->romMask + 1);
	heatBankSync(&comp->heatDiv, heat_div_size(comp));
}

void comp_heat_reset(Computer* comp) {
	heatBankReset(&comp->heatRam);
	heatBankReset(&comp->heatRom);
	heatBankReset(&comp->heatDiv);
}

void comp_heat_free(Computer* comp) {
	heatBankFree(&comp->heatRam);
	heatBankFree(&comp->heatRom);
	heatBankFree(&comp->heatDiv);
}

static const xHeatCell heat_nocell = {0, 0, 0, 0};

// which bank an absolute address lands in, and where in it. NULL when nothing
// is counted there - slot, ext and io cells are not tracked. every read and
// write of the counters goes through here, so the rule lives in one place.
// The Next's layer 2 reaches the SRAM behind its BRAM pages, which its view
// gives as an ext cell past the 2M image: that is the same SRAM page, counted there

static xHeatBank* heatBankFor(Computer* comp, int type, int abs, int* idx) {
	switch (type) {
		case MEM_RAM: *idx = abs & comp->mem->ramMask; return &comp->heatRam;
		case MEM_EXT:
			if (!heat_is_next(comp)) return NULL;
			*idx = next_shadow_sram(abs);
			return (*idx < 0) ? NULL : &comp->heatRam;
		case MEM_ROM: *idx = abs & comp->mem->romMask; return &comp->heatRom;
		case MEM_DIV: *idx = abs % (DIV_EE_SIZE + DIV_RAM_MAX); return &comp->heatDiv;
	}
	return NULL;
}

xHeatCell comp_heat_phys(Computer* comp, int type, int abs) {
	int idx;
	xHeatCell cell = heat_nocell;
	xHeatBank* bank = heatBankFor(comp, type, abs, &idx);
	if (bank && (idx >= 0) && (idx < bank->size)) {
		cell.rd = bank->rd[idx];
		cell.wr = bank->wr[idx];
		cell.ex = bank->ex[idx];
		cell.valid = 1;
	}
	return cell;
}

xHeatCell comp_heat_cpu(Computer* comp, int adr) {
	xAdr xadr = comp_get_xadr(comp, adr, MEM_VIEW_READ);
	return comp_heat_phys(comp, xadr.type, xadr.abs);
}

xHeatCell comp_heat_area(Computer* comp, int area, int page, int ofs) {
	int type, abs;
	if (!comp_cell_xadr(comp, area, page, ofs, &type, &abs)) return heat_nocell;
	return comp_heat_phys(comp, type, abs);
}

// a page starts on an 8K step in its bank and covers whole steps, so its sums
// are the totals of those steps
xHeatTotal comp_heat_page(Computer* comp, int area, int page) {
	xHeatTotal sum = {0, 0, 0, 0};
	int type, abs, idx;
	int unit = comp_cell_unit(comp, area);
	if (!comp_cell_xadr(comp, area, page, 0, &type, &abs)) return sum;
	xHeatBank* bank = heatBankFor(comp, type, abs, &idx);
	if (!bank || (idx < 0) || (idx + unit > bank->size)) return sum;
	for (int i = idx >> HEAT_TOT_SHIFT; i < (idx + unit) >> HEAT_TOT_SHIFT; i++) {
		sum.rd += bank->tot[i * 3 + HEAT_RD];
		sum.wr += bank->tot[i * 3 + HEAT_WR];
		sum.ex += bank->tot[i * 3 + HEAT_EX];
	}
	sum.valid = 1;
	return sum;
}

void comp_heat_hit(Computer* comp, int adr, int kind, int access) {
	int idx;
	xAdr xadr = comp_get_xadr(comp, adr, access);
	xHeatBank* bank = heatBankFor(comp, xadr.type, xadr.abs, &idx);
	if (bank) heatBankHit(bank, idx, kind);
}

// the areas in the order the export writes them, each under its own name
static const struct {int area; const char* name;} heat_areas[] = {
	{CELL_RAM, "RAM"}, {CELL_ROM, "ROM"}, {CELL_ALTROM, "ALTROM"}, {CELL_DIVROM, "DIVROM"},
	{CELL_DIV, "DIV"}, {CELL_MFROM, "MFROM"}, {CELL_MFRAM, "MFRAM"}
};

// dump to CSV: "type,page,offset,read,write,exec" per cell, decimal throughout. the type is
// the area, the page counts in the area's own unit (16K, the Next's RAM and divMMC 8K).
// every cell is written, including untouched ones (all-zero rows), so a reader can recover
// the exact size of every area without any extra size metadata.
int comp_heat_save(Computer* comp, const char* path) {
	FILE* file = fopen(path, "w");
	if (!file) return -1;
	fprintf(file, "type,page,offset,read,write,exec\n");
	for (unsigned i = 0; i < sizeof(heat_areas) / sizeof(heat_areas[0]); i++) {
		int area = heat_areas[i].area;
		int unit = comp_cell_unit(comp, area);
		int pages = comp_cell_pages(comp, area);
		for (int pg = 0; pg < pages; pg++) {
			for (int ofs = 0; ofs < unit; ofs++) {
				xHeatCell c = comp_heat_area(comp, area, pg, ofs);
				fprintf(file, "%s,%d,%d,%u,%u,%u\n", heat_areas[i].name, pg, ofs, c.rd, c.wr, c.ex);
			}
		}
	}
	fclose(file);
	return 0;
}
