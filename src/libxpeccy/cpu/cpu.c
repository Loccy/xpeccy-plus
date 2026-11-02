#include <stdlib.h>
#include "../xlog.h"
#include <string.h>
#include <stdio.h>
#include <string.h>

#include "cpu.h"

// TODO: cpu headers must be included cuz of callbacks, but flgX redefinition occurs
// buuuuuuuuut it doesn't matter from here  (no flgX macros used)
// undef redefined flgX after including
#include "Z80/z80.h"

// no-proc

const char nomnem[] = "undef";

xRegDsc nil_reg_tab[] = {
	{REG_EOT, "", 0, 0, NULL, NULL}
};

void nil_reset(CPU* cpu) {}
int nil_exec(CPU* cpu) {return 1;}
xAsmScan nil_asm(int adr, const char* com, char* buf) {
	xAsmScan res;
	res.match = 0;
	return res;
}
xMnem nil_mnem(CPU* cpu, int adr, cbmr mrd, void* data) {
	xMnem res;
	res.len = 1;
	res.mnem = nomnem;
	res.mem = 0;
	res.cond = 0;
	return res;
}
cpuCore dumCore = {CPU_NONE, "none", nil_reg_tab, 8, NULL, nil_reset, nil_exec, nil_asm, nil_mnem};

extern xRegDsc z80RegTab[];

cpuCore cpuTab[] = {
	{CPU_Z80, "Z80", z80RegTab, 16, z80_init, z80_reset, z80_exec, z80_asm, z80_mnem},
	// the same exec loop on the Z80N's tables, so fast mode sees a z80_exec core
	{CPU_Z80N, "Z80N", z80RegTab, 16, z80n_init, z80n_reset, z80_exec, z80n_asm, z80_mnem},
	{CPU_NONE, "none", nil_reg_tab, 8, NULL, nil_reset, nil_exec, nil_asm, nil_mnem}
};

cpuCore* findCore(int type) {
	int idx = 0;
	while ((cpuTab[idx].type != CPU_NONE) && (cpuTab[idx].type != type)) {
		idx++;
	}
	return &cpuTab[idx];
}

void cpuSetCore(CPU* cpu, cpuCore* core) {
	cpu->core = core;
	cpu->type = core->type;
	cpu->busmask = (1 << core->adrbus) - 1;
	cpu->pcdsc = find_reg_type(cpu, REG_PC);
	if (core->init) {
		core->init(cpu);
	}
}

// the core by its name in the built-in table
int cpu_set_type(CPU* cpu, const char* name) {
	cpuCore* itm = cpuTab;
	int res = 0;
	while ((itm->type != CPU_NONE) && !res) {
		if (!strcmp(itm->name, name)) {
			cpuSetCore(cpu, itm);
			res = 1;
		} else {
			itm++;
		}
	}
	return res;
}

int cpuSetType(CPU* cpu, int type) {
	cpuCore* core = findCore(type);
	if (core != NULL) {
		cpuSetCore(cpu, core);
	} else {
		cpuSetCore(cpu, &dumCore);
	}
	return core ? 1 : 0;
}

// A cpu with no machine behind it (the GS card's Z80) gets this, so cpu_irq
// and the direct xirq call in the exec loop never test for NULL.
static void cpu_dummy_irq(int t, void* ptr) {}

static void cpu_end_irq(CPU* cpu) {
	cpu->xirq(IRQ_CPU_ACK, cpu->xptr);
}

static void cpu_sync_irq(CPU* cpu) {
	cpu->xirq(IRQ_CPU_SYNC, cpu->xptr);
}

CPU* cpuCreate(int type, cbmr fmr, cbmw fmw, cbir fir, cbiw fiw, cbiack frq, cbirq xirq, void* dt) {
	CPU* cpu = (CPU*)malloc(sizeof(CPU));
	memset(cpu, 0x00, sizeof(CPU));
	cpu->xptr = dt;
	cpu->mrd = fmr;
	cpu->mwr = fmw;
	cpu->ird = fir;
	cpu->iwr = fiw;
	cpu->xack = frq;
	cpu->xirq = xirq ? xirq : cpu_dummy_irq;
	cpu->xend = cpu_end_irq;
	cpu->xsync = cpu_sync_irq;
	cpuSetType(cpu, type);
	return cpu;
}

void cpuDestroy(CPU* cpu) {
	free(cpu);
}

void cpu_reset(CPU* cpu) {
	if (!cpu->core) return;
	if (!cpu->core->reset) return;
	cpu->core->reset(cpu);
}

int cpu_int_ready(CPU* cpu) {
	if ((cpu->type != CPU_Z80) && (cpu->type != CPU_Z80N)) return 0;
	return z80_int_ready(cpu);
}

// disasm

static const char halfByte[] = "0123456789ABCDEF";
static char tmpbuf[1024];

// used to recover a return address that isn't encoded in the opcode (ret/reti/retn),
// by reading it straight off the stack instead
unsigned short cpu_peek_word(cbdmr mrd, void* data, int adr) {
	return (mrd(adr & 0xffff, data) & 0xff) | ((mrd((adr + 1) & 0xffff, data) & 0xff) << 8);
}

typedef struct {
	cbdmr read;
	void* data;
} cpuDasmReader;

static int cpu_dasm_read(int adr, int access, void* ptr) {
	cpuDasmReader* reader = (cpuDasmReader*)ptr;
	return reader->read(adr, reader->data);
}

xMnem cpuDisasm(CPU* cpu, int adr, char* buf, cbdmr mrd, void* data) {
	cpuDasmReader reader = {mrd, data};
	return cpuDisasmView(cpu, adr, buf, cpu_dasm_read, &reader);
}

xMnem cpuDisasmView(CPU* cpu, int adr, char* buf, cbmr mrd, void* data) {
	xMnem mn;
//	opCode* opt = cpu->tab;
	unsigned char op;
	unsigned char tmp;
	unsigned char dtl;
	unsigned char dth;
	unsigned short dtw;
	mn.mnem = NULL;
	mn.flag = 0;
	if (!buf) buf = tmpbuf;
//	if (opt == NULL) {			// no opcode tab
//		strcpy(buf, "undef");
//		mn.len = 1;
//	} else {
		mn = cpu->core->mnem(cpu, adr, mrd, data);
		// mn.oadr = -1;
		adr += mn.len;
		tmp = strstr(mn.mnem, ":5") ? mrd((adr - 2) & 0xffff, CPU_DASM_CODE, data) : 0;
		const char* src = mn.mnem;

		while (*src != 0) {
			if (*src == ':') {
				src++;
				op = *(src++);
				switch(op) {
					case '1':		// byte = (adr)
						dtl = mrd(adr++, CPU_DASM_CODE, data);
						mn.len++;
						*buf++ = '#';
						*buf++ = halfByte[dtl >> 4];
						*buf++ = halfByte[dtl & 0x0f];
						break;
					case '2':		// word = (adr,adr+1)
						dtl = mrd(adr++, CPU_DASM_CODE, data);
						dth = mrd(adr++, CPU_DASM_CODE, data);
						mn.len += 2;
						mn.oadr = dtl | (dth << 8);
						*buf++ = '#';
						*buf++ = halfByte[dth >> 4];
						*buf++ = halfByte[dth & 0x0f];
						*buf++ = halfByte[dtl >> 4];
						*buf++ = halfByte[dtl & 0x0f];
						break;
					case '3':		// word = adr + [e = (adr)]
						dtl = mrd(adr++, CPU_DASM_CODE, data);
						mn.len++;
						dtw = (adr + (signed char)dtl) & 0xffff;
						mn.oadr = dtw;
						*buf++ = '#';
						*buf++ = halfByte[(dtw >> 12) & 0x0f];
						*buf++ = halfByte[(dtw >> 8) & 0x0f];
						*buf++ = halfByte[(dtw >> 4) & 0x0f];
						*buf++ = halfByte[dtw & 0x0f];
						break;
					case '4':		// signed byte e = (adr)
						tmp = mrd(adr++, CPU_DASM_CODE, data);
						mn.len++;
					case '5':		// signed byte e = tmp
						if (tmp < 0x80) {
							*(buf++) = '+';
						} else {
							*(buf++) = '-';
							tmp = (0xff - tmp) + 1;
						}
						*buf++ = '#';
						*buf++ = halfByte[tmp >> 4];
						*buf++ = halfByte[tmp & 0x0f];
						break;
					case '6':		// word = (adr,adr+1), high byte first (Z80N push nn)
						dth = mrd(adr++, CPU_DASM_CODE, data);
						dtl = mrd(adr++, CPU_DASM_CODE, data);
						mn.len += 2;
						*buf++ = '#';
						*buf++ = halfByte[dth >> 4];
						*buf++ = halfByte[dth & 0x0f];
						*buf++ = halfByte[dtl >> 4];
						*buf++ = halfByte[dtl & 0x0f];
						break;
					case '7':		// = #adr
						*buf++ = '#';
						*buf++ = halfByte[(adr >> 12) & 0x0f];
						*buf++ = halfByte[(adr >> 8) & 0x0f];
						*buf++ = halfByte[(adr >> 4) & 0x0f];
						*buf++ = halfByte[adr & 0x0f];
						break;
					case ':':
						*buf++ = ':';
						break;
				}
			} else {
				*(buf++) = *(src++);
			}
		}
		*buf = 0;
//	}
	return mn;
}

// asm

const char letrz[] = "+-0123456789";

xAsmScan scanAsmTab(const char* com, opCode* tab) {
	xAsmScan res;
	res.match = 0;
	int i;
	for (i = 0; i < 8; i++)
		memset(res.arg[i], 0, 256);
	res.idx = -1;
	i = 0;
	int par = 0;
	int work;
	const char* cptr;
	const char* zptr;
	const char* tptr;
	do {
		cptr = com;
		tptr = tab[i].mnem;
		do {
			work = 0;
			if (*tptr == ':') {						// need argument
				if (strchr(letrz, *cptr)) {				// if there is a number (maybe signed)
					tptr += 2;
					zptr = strchr(cptr, *tptr);
					if (zptr || (*tptr == 0x00)) {
						if (par < 8) {
							strncpy(res.arg[par], cptr, zptr - cptr);
							if (!strchr(res.arg[par], ',')) {	// check if argument doesn't have ',' - wrong matching
								work = 1;
								cptr = zptr;
								par++;
							}
						}
					}
				}
			} else {							// check mnemonic char by char
				if (*cptr == *tptr) {					// compare current char
					if (*cptr == 0x00) {				// if both strings reach end
						res.match = 1;
						res.idx = i;
						res.op = &tab[i];
					} else {					// else go to next char
						work = 1;
						cptr++;
						tptr++;
					}
				}
			}
		} while (work);
		i++;
	} while (!res.match && (i < 256));
	return res;
}

// asm


int cpuAsm(CPU* cpu, const char* com, char* buf, unsigned short adr) {
	if (strlen(com) > 255) return 0;
	int par = 0;
	int num;
	int sign;
	char cbuf[256];
	const char* zptr;

	char* ptr = cbuf;	// cbuf = toLower(com)
	strcpy(cbuf, com);
	while (*ptr) {
		if ((*ptr >= 'A') && (*ptr <= 'Z')) {
			*ptr ^= 0x20;
		}
		ptr++;
	}

	xAsmScan res = cpu->core->asmbl(adr, cbuf, buf);
	if (res.match) {
		ptr = res.ptr;
		zptr = res.op->mnem;

		while ((par < 7) && (strlen(res.arg[par]) > 0)) {
			zptr = strchr(zptr, ':');
			if (!zptr) break;
			zptr++;
			num = strtol(res.arg[par], NULL, 0);
			sign = ((res.arg[par][0] == '+') || (res.arg[par][0] == '-')) ? 1 : 0;
			par++;
			switch(*zptr) {
				case '1':
					*ptr++ = num & 0xff;
					break;
				case '2':
					*ptr++ = num & 0xff;
					*ptr++ = (num >> 8) & 0xff;
					break;
				case '6':
					*ptr++ = (num >> 8) & 0xff;
					*ptr++ = num & 0xff;
					break;
				case '3':
					if (!sign) {
						num -= (adr + (ptr - buf + 1));
					}
					*ptr++ = num & 0xff;
					break;
				case '4':
					if (sign) {
						*ptr++ = num & 0xff;
					} else {
						ptr = buf;
						par = 100;
					}
					break;
				case '5':
					if (sign) {
						*ptr = *(ptr-1);
						*(ptr-1) = num & 0xff;
						ptr++;
					} else {
						ptr = buf;
						par = 100;
					}
					break;
			}
		}
	} else {
		ptr = buf;
	}
	return (ptr - buf);
}

// get/set reg bunch

xRegDsc* find_reg_id(CPU* cpu, int id) {
	xRegDsc* rd = cpu->core->rdsctab;
	while ((rd->id != REG_EOT) && (rd->id != id)) {
		rd++;
	}
	return (rd->id == REG_EOT) ? NULL : rd;
}

xRegDsc* find_reg_name(CPU* cpu, const char* name) {
	xRegDsc* rd = cpu->core->rdsctab;
	int work = 1;
	while (work && (rd->id != REG_EOT)) {
		if (!strcmp(rd->name, name)) {
			work = 0;
		} else {
			rd++;
		}
	}
	return work ? NULL : rd;
}

xRegDsc* find_reg_type(CPU* cpu, int type) {
	xRegDsc* rd = cpu->core->rdsctab;
	int work = 1;
	while (work && (rd->id != REG_EOT)) {
		if ((rd->flag & REG_TYPE_M) == type) {
			work = 0;
		} else {
			rd++;
		}
	}
	return work ? NULL : rd;
}

xRegBunch cpuGetRegs(CPU* cpu) {
	xRegBunch bunch;
	int i = 0;
	xRegDsc* rd = cpu->core->rdsctab;
	xRegister* reg = bunch.regs;
	while (rd->id != REG_EOT) {
		if ((rd->id != REG_EMPTY) && (i < 32)) {
			reg->name = rd->name;
			reg->id = rd->id;
			reg->size = rd->size;
			reg->flag = rd->flag;
			reg->pair = rd->pair;
			reg->group = rd->group;
			reg->value = rd->get ? rd->get(cpu) : -1;
			reg++;
			i++;
		}
		rd++;
	}
	if (i < 32) {
		reg->id = REG_EOT;	// mark end of bunch
	}
	bunch.flags = rd->name;		// 'end of table' item name is flag names
	return bunch;
}

int reg_get_value(CPU* cpu, xRegDsc* dsc) {
	return dsc->get ? dsc->get(cpu) : -1;
}

int reg_set_value(CPU* cpu, xRegDsc* dsc, int val) {
	int res = 1;
	if (dsc->set) {
		dsc->set(cpu, val);
	} else {
		res = 0;
	}
	return res;
}

// f is pointer to bool variable: false if register doesn't exists
int cpu_get_reg(CPU* cpu, const char* name, bool* f) {
	int res = -1;
	bool err = true;
	xRegDsc* rd = find_reg_name(cpu, name);
	if (rd) {
		res = reg_get_value(cpu, rd);
		err = (res < 0);
	} else {
		err = true;
	}
	if (f != NULL) {*f = err;}
	return res;
}

int cpu_set_reg(CPU* cpu, const char* name, int val) {
	xRegDsc* rd = find_reg_name(cpu, name);
	if (rd) reg_set_value(cpu, rd, val);
	return rd ? 1 : 0;
}

void cpuSetRegs(CPU* cpu, xRegBunch bunch) {
	xRegister* reg = bunch.regs;
	xRegDsc* rd;
	while (reg->id != REG_EOT) {
		rd = find_reg_id(cpu, reg->id);
		if (rd) {
			if (rd->set) {
				rd->set(cpu, reg->value);
			}
//			printf("%s = %X\n", rd->name, rd->get(cpu));
		}
		reg++;
	}
}

int cpu_get_regtype(CPU* cpu, int type) {
	xRegDsc* rd = find_reg_type(cpu, type);
	return rd ? reg_get_value(cpu, rd) : -1;
}

void cpu_set_regtype(CPU* cpu, int type, int val) {
	xRegDsc* rd = find_reg_type(cpu, type);
	if (rd) reg_set_value(cpu, rd, val);
}

int cpu_get_sp(CPU* cpu) {return cpu_get_regtype(cpu, REG_SP);}
int cpu_get_pc(CPU* cpu) {return cpu->pcdsc ? reg_get_value(cpu, cpu->pcdsc) : -1;}
void cpu_set_pc(CPU* cpu, int val) {cpu_set_regtype(cpu, REG_PC, val);}
// TODO: remove get/set flags callbacks from cpu core, flag register is marked as REG_FLG in cpu->core->rdtab and have get/set callbacks
int cpu_get_flag(CPU* cpu) {return cpu_get_regtype(cpu, REG_FLG);}
void cpu_set_flag(CPU* cpu, int v) {cpu_set_regtype(cpu, REG_FLG, v);}
