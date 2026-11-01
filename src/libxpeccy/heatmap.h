#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// memory access counters: separate read/write/exec hit counts per cell.
// arrays are allocated dynamically, sized to match the currently
// active hardware's actual ram/rom capacity (see comp_heat_sync).
typedef struct {
	unsigned int* rd;
	unsigned int* wr;
	unsigned int* ex;
	unsigned long long* tot;	// read/write/exec sums of every 8K, three in a row
	int size;		// number of cells currently allocated
} xHeatBank;

enum {
	HEAT_RD = 0,
	HEAT_WR,
	HEAT_EX
};

#define HEAT_TOT_SHIFT	13	// the totals' step: 8K, the smallest page of any area

struct Computer;

// one cell's counters, as read back for display. `valid` is 0 when the address
// is not backed by tracked memory (io/slot cells are never counted)
typedef struct {
	unsigned int rd;
	unsigned int wr;
	unsigned int ex;
	int valid;
} xHeatCell;

// the sums of a whole page
typedef struct {
	unsigned long long rd;
	unsigned long long wr;
	unsigned long long ex;
	int valid;
} xHeatTotal;

void heatBankFree(xHeatBank*);

void comp_heat_sync(struct Computer*);		// (re)allocate the banks to match current mem sizes
void comp_heat_reset(struct Computer*);		// zero all counters, keep allocation
void comp_heat_free(struct Computer*);		// release the banks (call on compDestroy)
void comp_heat_hit(struct Computer*, int adr, int kind, int access);
int comp_heat_save(struct Computer*, const char* path);	// returns 0 on success

xHeatCell comp_heat_cpu(struct Computer*, int adr);			// cell behind a cpu address, through the current paging
xHeatCell comp_heat_phys(struct Computer*, int type, int abs);		// cell by absolute address in MEM_RAM/MEM_ROM/MEM_DIV
xHeatCell comp_heat_area(struct Computer*, int area, int page, int ofs);	// cell by its area (CELL_*), page and offset
xHeatTotal comp_heat_page(struct Computer*, int area, int page);	// the sums of one page of an area

#ifdef __cplusplus
}
#endif
