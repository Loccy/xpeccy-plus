#include "filetypes.h"
#include "../cpu/Z80/z80.h"

#pragma pack (push, 1)

typedef struct {
	unsigned char i;
	unsigned char _l,_h,_e,_d,_c,_b,_f,_a;
	unsigned char l,h,e,d,c,b,ly,hy,lx,hx;
	unsigned char flag19;
	unsigned char r;
	unsigned char f,a,lsp,hsp;
	unsigned char imod,border;
} snaHead;

#pragma pack (pop)

// the size is all there is: 49179 bytes for a 48K, more for a 128K
int sna_hardware_of(size_t size) {
	return (size < 49180) ? SNAP_HW_48K : SNAP_HW_128K;
}

int loadSNA_f(Computer* comp, FILE* file, size_t fileSize) {
	comp->sndSeed = 1;

	unsigned char tmp, tmp2;
	//unsigned short adr;
	char pageBuf[0x4000];
	char tmpgBuf[0x4000];
	int is48 = (sna_hardware_of(fileSize) == SNAP_HW_48K);
	// header and three pages; a 128K one has the pc, two ports and five pages
	// more - six when the paged-in bank is 2 or 5, which is read below
	if (fileSize < (is48 ? 49179 : 131103)) return ERR_SNAP_SHORT;

	comp_snap_reset(comp, is48 ? RES_48 : RES_128);

	snaHead hd;
	if (fread((char*)&hd, sizeof(snaHead), 1, file) != 1) return ERR_SNAP_SHORT;
	comp->cpu->regHLa = (hd._h << 8) | hd._l;
	comp->cpu->regDEa = (hd._d << 8) | hd._e;
	comp->cpu->regBCa = (hd._b << 8) | hd._c;
	comp->cpu->regAa = hd._a;
	comp->cpu->regFa = hd._f;
	comp->cpu->regHL = (hd.h << 8) | hd.l;
	comp->cpu->regDE = (hd.d << 8) | hd.e;
	comp->cpu->regBC = (hd.b << 8) | hd.c;
	comp->cpu->regA = hd.a;
	cpu_set_flag(comp->cpu, hd.f);
	comp->cpu->regIX = (hd.hx << 8) | hd.lx;
	comp->cpu->regIY = (hd.hy << 8) | hd.ly;
	comp->cpu->regSP = (hd.hsp << 8) | hd.lsp;
	comp->cpu->regI = hd.i;
	comp->cpu->regR = hd.r;
	comp->cpu->regR7 = hd.r & 0x80;
	comp->cpu->regIM = hd.imod & 3;
	// the byte is IFF2, which the RETN that ends a .sna copies into IFF1
	comp->cpu->flgIFF1 = !!(hd.flag19 & 4);
	comp->cpu->flgIFF2 = comp->cpu->flgIFF1;
	comp->cpu->inten = Z80_NMI | (comp->cpu->flgIFF1 ? Z80_INT : 0);
	comp->vid->brdcol = hd.border & 7;
	comp->vid->nextbrd = hd.border & 7;

	if (fread(pageBuf, 0x4000, 1, file) != 1) return ERR_SNAP_SHORT;
	memPutData(comp->mem,MEM_RAM,5,MEM_16K,pageBuf);
	if (fread(pageBuf, 0x4000, 1, file) != 1) return ERR_SNAP_SHORT;
	memPutData(comp->mem,MEM_RAM,2,MEM_16K,pageBuf);
	if (fread(tmpgBuf, 0x4000, 1, file) != 1) return ERR_SNAP_SHORT;

	if (is48) {
		comp->p7FFD = 0x10;
		comp->pEFF7 = 0x00;
		comp->flgROM = 1;		// set basic 48
		comp->flgDOS = 0;
		comp->tsconf.p21af &= ~0x08;		// rom @ 0000
		comp->tsconf.Page0 &= 0xfc;		// set page0 if 21af.b2=1
		comp->tsconf.Page0 |= 3;		// rom48k
		comp->hw->mapMem(comp);
		memSetBank(comp->mem, 0xc0, MEM_RAM, 0, MEM_16K, NULL,NULL,NULL);
		memPutData(comp->mem, MEM_RAM, 0, MEM_16K, tmpgBuf);
		comp->vid->vidPage = 5;
		comp->cpu->regPCl = memRd(comp->mem, comp->cpu->regSP++);
		comp->cpu->regPCh = memRd(comp->mem, comp->cpu->regSP++);
	} else {
		comp->cpu->regPC = fgetw(file);
		tmp = fgetc(file);		// byte out to 7ffd. b0..2 current page
		comp->flgBDI = 0;
		tmp2 = fgetc(file);
		comp->p7FFD &= ~0x20;
		comp->flgROM = (tmp & 0x10) ? 1 : 0;
		comp->flgDOS = (tmp2 & 1) ? 1 : 0;
		comp->tsconf.p21af &= ~0x08;		// rom @ 0000
		comp->tsconf.Page0 &= 0xfc;		// set page0 if 21af.b2=1
		comp->tsconf.Page0 |= ((comp->flgROM) ? 1 : 0) | (comp->flgDOS ? 0 : 2);
		snap_plus3_basic48(comp, SNAP_HW_128K, tmp);
		comp->hw->out(comp, 0x7ffd, tmp);

		for (tmp2 = 0; tmp2 < 8; tmp2++) {
			if ((tmp2 == 2) || (tmp2 == 5)) tmp2++;
			if ((tmp & 7) != tmp2) {
				if (fread(pageBuf, 0x4000, 1, file) != 1) return ERR_SNAP_SHORT;
				memPutData(comp->mem, MEM_RAM, tmp2, MEM_16K, pageBuf);
			}
		}
		memPutData(comp->mem, MEM_RAM, tmp & 7, MEM_16K, tmpgBuf);
	}
	return ERR_OK;
}

int loadSNA(Computer* comp, const char* name, int drv) {
	FILE* file = fopen(name,"rb");
	if (!file) return ERR_CANT_OPEN;
	fseek(file, 0, SEEK_END);
	size_t fileSize = ftell(file);
	rewind(file);
	int res = loadSNA_f(comp, file, fileSize);
	fclose(file);
	if (res == ERR_OK) {
		comp_set_frame_tick(comp, -1);		// the file does not say where in the frame
	}
	return res;
}

// A .sna names no model, and 48 BASIC is the same on all of them: only one
// running the 128 editor (7FFD bit 4 clear, the PC in ROM) is in a ROM of its own
// model, and sna_editor_of() is left to say which. One running a program is
// left to any 128K.
int snaGetHardware(const char* name) {
	FILE* file = fopen(name, "rb");
	if (!file) return SNAP_HW_UNKNOWN;
	size_t size = fgetSize(file);
	int res = sna_hardware_of(size);
	unsigned char buf[3];			// 49179: PC, 49181: 7FFD
	if ((res == SNAP_HW_128K) && !fseek(file, 49179, SEEK_SET) && (fread(buf, 1, 3, file) == 3)
			&& !(buf[2] & 0x10)) {
		int pc = buf[0] | (buf[1] << 8);
		unsigned char hd[2];		// 23: SP
		if ((pc == 0x0038) && !fseek(file, 23, SEEK_SET) && (fread(hd, 1, 2, file) == 2)) {
			int sp = hd[0] | (hd[1] << 8);	// as the interrupt came: where it came at
			int bank = buf[2] & 7;
			// the file holds banks 5, 2 and the paged one, in that order
			long top = (bank == 5) ? 0 : (bank == 2) ? 0x4000 : 0x8000;
			long off = 27 + ((sp < 0xc000) ? sp - 0x4000 : top + (sp & 0x3fff));
			if ((sp >= 0x4000) && (sp < 0xffff) && !fseek(file, off, SEEK_SET) && (fread(hd, 1, 2, file) == 2))
				pc = hd[0] | (hd[1] << 8);
		}
		if (pc < 0x4000) res |= SNAP_IN_ROM;
	}
	fclose(file);
	return res;
}

static int sna_after_call(const unsigned char* rom, int adr) {
	static const unsigned char calls[] = {0xcd, 0xc4, 0xcc, 0xd4, 0xdc, 0xe4, 0xec, 0xf4, 0xfc};
	if ((adr >= 1) && ((rom[adr - 1] & 0xc7) == 0xc7)) return 1;		// RST n
	if (adr >= 3)
		for (size_t i = 0; i < sizeof(calls); i++)
			if (rom[adr - 3] == calls[i]) return 1;
	return 0;
}

// Which of the 128 editors given (16K images) a 128K .sna with its editor paged
// in was taken with, -1 when the file does not tell. First the routines the
// editor copies to #5B00 at start - the +3's are its own, the 128K's and the
// +2's the same - then the words on the stack that point into the ROM: a return
// address follows a CALL or an RST in the editor that pushed it.
int sna_editor_of(const char* name, const unsigned char* const* eds, int count) {
	FILE* file = fopen(name, "rb");
	if (!file) return -1;
	static unsigned char buf[0x24000];
	size_t len = fread(buf, 1, sizeof(buf), file);
	fclose(file);
	if ((len < 131103) || (buf[49181] & 0x10) || (count > 8)) return -1;
	int bank = buf[49181] & 7;
	const unsigned char* page[4] = {NULL, buf + 27, buf + 27 + 0x4000, buf + 27 + 0x8000};	// 5, 2, the paged one
	if ((bank == 5) || (bank == 2)) page[3] = page[(bank == 5) ? 1 : 2];
	int cand[8];
	int n = 0;
	const unsigned char* swp = page[1] + 0x1b00;
	for (int i = 0; i < count; i++) {
		cand[i] = 0;
		for (int a = 0; a <= 0x4000 - 32; a++) {
			if (!memcmp(eds[i] + a, swp, 32)) {
				cand[i] = 1;
				n++;
				break;
			}
		}
	}
	if (n == 1) {
		for (int i = 0; i < count; i++)
			if (cand[i]) return i;
	}
	if (n == 0) {				// overwritten: let the stack alone decide
		for (int i = 0; i < count; i++) cand[i] = 1;
	}
	int sp = buf[23] | (buf[24] << 8);
	int score[8] = {0};
	for (int k = 0; k < 16; k++) {
		int adr = (sp + 2 * k) & 0xffff;
		if ((adr < 0x4000) || (adr == 0xffff)) break;
		const unsigned char* p = page[adr >> 14] + (adr & 0x3fff);
		int w = p[0] | (p[1] << 8);
		if ((w < 0x40) || (w >= 0x4000)) continue;
		for (int i = 0; i < count; i++)
			if (cand[i]) score[i] += sna_after_call(eds[i], w);
	}
	int best = -1;
	int top = 0;
	for (int i = 0; i < count; i++) {
		if (score[i] > top) {
			top = score[i];
			best = i;
		} else if (score[i] == top) {
			best = -1;
		}
	}
	return best;
}

int saveSNA(Computer* comp, const char* name, int drv) {
	if (comp->hw->id == HW_ZXNEXT) return ERR_SNAP_NEXT;	// a 128K's state is not the Next's
	FILE* file = fopen(name, "wb");
	if (!file) return ERR_CANT_OPEN;
	unsigned char bnk, i;
	int sna48 = (comp->mem->ramSize < MEM_128K) ? 1 : 0;
	// A 48K snapshot has nowhere to keep the pc, so it goes on the machine's
	// own stack. That is a write to a running machine: what stood there and the
	// stack pointer both go back once the file is written.
	int sp = comp->cpu->regSP;
	unsigned char stk[2] = {0, 0};
	if (sna48) {
		stk[0] = memRd(comp->mem, (sp - 1) & 0xffff);
		stk[1] = memRd(comp->mem, (sp - 2) & 0xffff);
		memWr(comp->mem, --comp->cpu->regSP, comp->cpu->regPCh);
		memWr(comp->mem, --comp->cpu->regSP, comp->cpu->regPCl);
	}
	snaHead hd;
	xreg16 rp;
	memset(&hd, 0, sizeof(snaHead));
	rp.w = comp->cpu->regHLa; hd._h = rp.h; hd._l = rp.l;
	rp.w = comp->cpu->regDEa; hd._d = rp.h; hd._e = rp.l;
	rp.w = comp->cpu->regBCa; hd._b = rp.h; hd._c = rp.l;
	hd._a = comp->cpu->regAa; hd._f = comp->cpu->regFa;
	hd.h = comp->cpu->regH; hd.l = comp->cpu->regL;
	hd.d = comp->cpu->regD; hd.e = comp->cpu->regE;
	hd.b = comp->cpu->regB; hd.c = comp->cpu->regC;
	hd.a = comp->cpu->regA; hd.f = cpu_get_flag(comp->cpu);
	hd.hx = comp->cpu->regIXh; hd.lx = comp->cpu->regIXl;
	hd.hy = comp->cpu->regIYh; hd.ly = comp->cpu->regIYl;
	hd.hsp = comp->cpu->regSPh; hd.lsp = comp->cpu->regSPl;
	hd.i = comp->cpu->regI;
	hd.r = z80_get_r(comp->cpu);	// bit 7 is kept apart from the counter
	hd.imod = comp->cpu->regIM;
	hd.flag19 = comp->cpu->flgIFF1 ? 4 : 0;
	hd.border = comp->vid->brdcol & 7;
	fwrite((char*)&hd, sizeof(snaHead), 1, file);

	fwrite(comp->mem->ramData + ((5 << 14) & comp->mem->ramMask), MEM_16K, 1, file);	// page 5
	fwrite(comp->mem->ramData + ((2 << 14) & comp->mem->ramMask), MEM_16K, 1, file);	// page 2
	if (sna48) {
		fwrite(comp->mem->ramData, MEM_16K, 1, file);		// page 0
	} else {
		bnk = comp->p7FFD & 7;
		fwrite(comp->mem->ramData + ((bnk << 14) & comp->mem->ramMask), MEM_16K, 1, file);	// current page
		fputc(comp->cpu->regPCl, file);				// pc
		fputc(comp->cpu->regPCh, file);
		fputc(comp->p7FFD, file);				// 7ffd
		fputc(comp->flgDOS ? 0xff : 0x00, file);			// trdos
		for (i = 0; i < 8; i++) {				// all others pages
			if ((i == 2) || (i == 5)) i++;
			if (i != bnk) {
				fwrite(comp->mem->ramData + ((i << 14) & comp->mem->ramMask), MEM_16K, 1, file);
			}
		}
	}
	if (sna48) {			// give the machine its stack back
		memWr(comp->mem, comp->cpu->regSP, stk[1]);
		memWr(comp->mem, (comp->cpu->regSP + 1) & 0xffff, stk[0]);
		comp->cpu->regSP = sp;
	}
	fclose(file);
	return ERR_OK;
}
