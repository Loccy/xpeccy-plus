#include "sdcard.h"
#include "fatfile.h"
#include "xlog.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// commands
#define	CMD00		0x00	// + soft reset
#define	CMD01		0x01	// out of idle state
#define	CMD08		0x08	// +
#define	CMD09		0x09	// ask card to send card speficic data (CSD)
#define	CMD10		0x0a	// ask card to send card identification (CID)
#define	CMD12		0x0c	// + stop transmission on multiple block read
#define	CMD13		0x0d	// ask the card to send it's status register
#define	CMD16		0x10    // + sets the block length used by the memory card
#define	CMD17		0x11	// + read single block
#define	CMD18		0x12	// + read multiple block
#define	CMD24		0x18	// + writes a single block
#define	CMD25		0x19	// + writes multiple blocks
#define	CMD27		0x1B	// change the bits in CSD
#define	CMD28		0x1C	// sets the write protection bit
#define	CMD29		0x1D    // clears the write protection bit
#define	CMD30		0x1E	// checks the write protection bit
#define	CMD32		0x20	// Sets the address of the first sector of the erase group
#define	CMD33		0x21	// Sets the address of the last sector of the erase group
#define	CMD34		0x22	// removes a sector from the selected group
#define	CMD35		0x23	// Sets the address of the first group
#define	CMD36		0x24	// Sets the address of the last erase group
#define	CMD37		0x25	// removes a group from the selected section
#define	CMD38		0x26	// erase all selected groups
#define	ACMD41		0x29	// +
#define	CMD42		0x2A	// locks a block
#define	CMD55		0x37	// + ACMD prefix
#define	CMD58		0x3A	// + reads the OCR register
#define	CMD59		0x3B	// + turn CRC

// A spec card is what a card of its size would be: up to 2 GB an SDSC one
static void sdc_set_kind(SDCard* sdc) {
	if (sdc->spec) sdc->sdhc = (sdc->maxlba > (0x80000000u >> 9));
}

// The machine's reset: the card keeps its power, so it drops the command it was
// in but stays initialised - a firmware that finds it back in idle takes it
// for gone (ZX Evo's service rom: "SD card lost").
void sdcReset(SDCard* sdc) {
	sdc->state = SDC_FREE;
	sdc->argCnt = 0;
	sdc->acmd = 0;
	sdc->cont = 0;
	sdc->respCnt = 0;
	sdc->buf.pos = -1;
}

// power-up or CMD0: back to idle, waiting for ACMD41
static void sdc_idle(SDCard* sdc) {
	sdcReset(sdc);
	sdc->idle = 1;
}

SDCard* sdcCreate() {
	SDCard* sdc = (SDCard*)malloc(sizeof(SDCard));
	if (!sdc) return NULL;
	memset(sdc,0x00,sizeof(SDCard));
	sdc->argCnt = 6;
	sdc->image = NULL;
	sdc->blkSize = 512;
	sdc->file = NULL;
	sdc_idle(sdc);
	return sdc;
}

void sdcDestroy(SDCard* sdc) {
	sdcCloseFile(sdc);
	free(sdc);
}

void sdcSetImage(SDCard* sdc, const char* name) {
	sdcCloseFile(sdc);
	if (strlen(name) == 0) {
		if (sdc->image) free(sdc->image);
		sdc->image = NULL;
		sdc->file = NULL;
	} else {
		sdc->image = realloc(sdc->image,strlen(name) + 1);
		strcpy(sdc->image,name);
		sdcOpenFile(sdc);
	}
}

// mount a synthetic volume built from a host folder. The card takes ownership
// of the volume; the folder path is kept in ->image, so everything checking for
// an inserted card keeps working.
void sdcSetFolder(SDCard* sdc, const char* name, vFat* vf) {
	sdcSetUnread(sdc, vf ? name : NULL);
	if (!vf) return;
	sdc->vfat = vf;
	sdc->maxlba = vf->volume;
	sdc->capacity = vf->volume >> 11;	// 512-byte sectors -> Mb
	sdc_set_kind(sdc);
	sdcSetLock(sdc, 0);
}

// A folder card a machine without the slot holds: the path only, read when a machine
// with the slot comes up (sdc_remount)
void sdcSetUnread(SDCard* sdc, const char* name) {
	sdcCloseFile(sdc);
	if (sdc->image) free(sdc->image);
	sdc->image = NULL;
	if (!name || !*name) return;
	sdc->image = (char*)malloc(strlen(name) + 1);
	strcpy(sdc->image, name);
}

int sdc_unread(SDCard* sdc) {
	return sdc->image && !sdc->vfat && !sdc->file;
}

// the card's lock switch, the user's; a folder that takes no writes is protected whatever it says
void sdcSetLock(SDCard* sdc, int lock) {
	sdc->lock = lock ? 1 : 0;
}

int sdc_protected(SDCard* sdc) {
	return sdc->lock || (sdc->vfat && !sdc->vfat->writable);
}

// file operation

void sdcRdSector(SDCard* sdc) {
//	printf("SDC read sector %i\n",sdc->addr);
	if ((sdc->addr < sdc->maxlba) && sdc->vfat) {
		vfat_read(sdc->vfat, sdc->addr, sdc->buf.data + 1);
	} else if ((sdc->addr < sdc->maxlba) && sdc->file) {
		x_fseek(sdc->file, (long long)sdc->addr << 9, SEEK_SET);
		if (fread(sdc->buf.data + 1, 512, 1, sdc->file) != 1)		// past the end of a rounded up image
			memset(sdc->buf.data + 1, 0xff, 512);
	} else {
		memset((void*)&sdc->buf.data[1],0xff,512);
	}
}

int sdc_read_lba(SDCard* sdc, unsigned int lba, unsigned char* buf) {
	if (lba >= sdc->maxlba) return 0;
	if (sdc->vfat) {
		vfat_read(sdc->vfat, lba, buf);
		return 1;
	}
	if (!sdc->file) return 0;
	x_fseek(sdc->file, (long long)lba << 9, SEEK_SET);
	return fread(buf, 512, 1, sdc->file) == 1;
}

static int sdc_fat_sector(void* ctx, unsigned int lba, unsigned char* buf) {
	return sdc_read_lba((SDCard*)ctx, lba, buf);
}

int sdc_has_file(SDCard* sdc, const char* path) {
	unsigned char b;
	return sdc->image && (fat_read_file(sdc_fat_sector, sdc, path, &b, 1) >= 0);
}

unsigned long long sdc_dir_print(SDCard* sdc) {
	return sdc->image ? fat_dir_print(sdc_fat_sector, sdc) : 0;
}

// 0 when the sector was refused: a folder that would not take it
int sdcWrSector(SDCard* sdc) {
//	printf("SDC write sector %i\n",sdc->addr);
	if (x_runahead) return 1;		// this frame is going to be rolled back
	sdc->writes++;
	if (sdc->vfat) {
		if ((sdc->addr >= sdc->maxlba) || !vfat_write(sdc->vfat, sdc->addr, sdc->buf.data + 1)) return 0;
		x_media_writes++;
		return 1;
	}
	if ((sdc->addr < sdc->maxlba) && sdc->file) {
		x_fseek(sdc->file, (long long)sdc->addr << 9, SEEK_SET);
		x_media_writes++;
		fwrite(sdc->buf.data + 1,512,1,sdc->file);
	}
	return 1;
}

// io operations

int sdcRead(SDCard* sdc) {
	if (!sdc->image || !sdc->on || sdc->cs) return 0xff;	// no image or OFF or !CS
	int res = -1;
	if (sdc->respCnt > 0) {				// if have response
		res = sdc->resp[sdc->respPos];
//		printf("resp %.2X\n",res);
		sdc->respPos++;
		sdc->respCnt--;
	} else {
		switch (sdc->state) {
			case SDC_READ:
				if (sdc->buf.pos < 0) {
					sdcRdSector(sdc);
					sdc->buf.data[0] = 0xfe;
					sdc->buf.data[513] = 0x00;	// TODO: data crc
					sdc->buf.data[514] = 0x00;
					sdc->buf.pos = 0;
				}
				res = sdc->buf.data[sdc->buf.pos];
				// printf("buf %X = %.2X\n",sdc->buf.pos,res);
				sdc->buf.pos++;
				if (sdc->buf.pos > 514) {		// 1token.512b.2crc = 0..514 = 515 bytes
					sdc->buf.pos = -1;
					if (sdc->cont) {
						sdc->addr++;
						sdc->buf.pos = -1;
					} else {
						sdc->state = SDC_FREE;
					}
				}
				break;
		}
	}
	return res;
}

// Every answer carries the idle bit until the card is brought out of idle with
// ACMD41. A host that takes R1 = 0 for "not an SD card" then falls back to the
// byte addressing an old card uses, and reads at 512 times the address it means.
void sdcR1(SDCard* sdc, int resp) {
	sdc->respCnt = 1;
	sdc->respPos = 0;
	sdc->resp[0] = (resp | (sdc->idle ? R1_IDLE : 0)) & 0xff;
}

unsigned int sdcGetArg(SDCard* sdc, unsigned int mask) {
	unsigned int res = sdc->arg[4] | (sdc->arg[3] << 8) | (sdc->arg[2] << 16) | (sdc->arg[1] << 24);
	return (res & mask);
}

void sdcExec(SDCard* sdc) {
//	printf("SDC exec %.2X\n",sdc->arg[0] & 0x3f);
	if ((sdc->arg[0] & 0xc0) == 0x40) {		// %01xxxxxx : a command token
		if (sdc->acmd) {
//			printf("SD ACMD%.2i\n",sdc->arg[0] & 0x3f);
			sdc->acmd = 0;		// reset ACMD
			switch (sdc->arg[0] & 0x3f) {
				case ACMD41:			// out of idle: the card is ready
					sdc->idle = 0;
					sdcR1(sdc,0);
					break;
				default:
					xlog(XLG_DISK, XLL_WARN, "undef ACMD %.2X",sdc->arg[0] & 0x3f);
					// assert(0);
					break;
			}
		} else {
//			printf("SD CMD%.2i\n",sdc->arg[0] & 0x3f);
			switch (sdc->arg[0] & 0x3f) {
				case CMD00:			// soft reset: whatever it was doing is dropped
					sdc_idle(sdc);
					sdcR1(sdc,0);
					break;
				case CMD08:
					sdc->resp[0] = sdc->idle ? R1_IDLE : 0;
					sdc->resp[1] = sdc->arg[1];	// return 4 bytes of argument (ORLY)
					sdc->resp[2] = sdc->arg[2];
					sdc->resp[3] = sdc->arg[3];
					sdc->resp[4] = sdc->arg[4];
					sdc->respCnt = 5;
					sdc->respPos = 0;
					break;
				case CMD12:				// stop multiple block rd/wr
					sdc->cont = 0;
					sdcR1(sdc,0);
					if (sdc->buf.pos < 0) sdc->state = SDC_FREE;
					break;
				case CMD16:
					sdc->blkSize = sdcGetArg(sdc,0xffffffff);
					sdcR1(sdc,0);
					break;
				case CMD17:				// read block
				case CMD18:				// read multiple block
					sdc->cont = ((sdc->arg[0] & 0x3f) == CMD18);
					sdc->addr = sdcGetArg(sdc,0xffffffff);
					if (sdc->addr < sdc->maxlba) {
						sdcR1(sdc,0);
						sdc->state = SDC_READ;
						sdc->buf.pos = -1;
					} else {
						sdcR1(sdc,R1_ADDRESS_ERR);
						sdc->cont = 0;
						sdc->state = SDC_FREE;
					}
					break;
				case CMD24:				// write block
				case CMD25:				// write multiple block
					sdc->cont = ((sdc->arg[0] & 0x3f) == CMD25);
					sdc->addr = sdcGetArg(sdc,0xffffffff);
					if (sdc->addr < sdc->maxlba) {
						sdcR1(sdc,0);
						sdc->state = SDC_WRITE;
						sdc->buf.pos = 0;
					} else {
						sdcR1(sdc,R1_ADDRESS_ERR);
						sdc->cont = 0;
						sdc->state = SDC_FREE;
					}
					break;
				case CMD55:
					sdc->acmd = 1;			// next command is ACMD
					sdcR1(sdc,0);
					break;
				case CMD58:				// read OCR register
					sdc->resp[0] = sdc->idle ? R1_IDLE : 0;
					sdc->resp[1] = 0xc0;		// powerup ready.sdh(x)c
					sdc->resp[2] = 0xff;		// all voltage is supported
					sdc->resp[3] = 0x80;
					sdc->resp[4] = 0x00;
					sdc->respCnt = 5;
					sdc->respPos = 0;
					break;
				case CMD59:
					sdc->checkCrc = sdc->arg[4] & 1;
					sdcR1(sdc,0);
					break;
				default:
					xlog(XLG_DISK, XLL_WARN, "undef CMD %.2X",sdc->arg[0] & 0x3f);
					// assert(0);
					break;
			}
		}
	} else {
		sdcR1(sdc,R1_ILLEGAL);	// Illegal command
	}
}

void sdcWrite(SDCard* sdc, int val) {
	if (!sdc->image || !sdc->on || sdc->cs) return;
//	printf("SD wr %.2X\n",val);
	if (sdc->state == SDC_WRITE) {
		if ((sdc->buf.pos == 0) && (val == 0xff)) {
			// the host's gap before the data token: nothing yet
		} else if (((sdc->arg[0] & 0x3f) == CMD25) && (sdc->buf.pos == 0) && (val == 0xfd)) {	// CMD25 stop token
			sdc->state = SDC_FREE;
		} else {
			sdc->buf.data[sdc->buf.pos] = val & 0xff;
			sdc->buf.pos++;
			if (sdc->buf.pos > 514) {	// 0..514 = 515 bytes = {1token,512data,2crc}
				if (sdc_protected(sdc) || !sdcWrSector(sdc)) {
					sdcR1(sdc,0x1d);	// xxx01101 - data rejected due write error
					sdc->state = SDC_FREE;
				} else {
					sdcR1(sdc,0x05);	// xxx00101 - data accepted
					if (sdc->cont) {
						sdc->busy = 1;
						sdc->buf.pos = 0;
						sdc->addr++;
					} else {
						sdc->state = SDC_FREE;
					}
				}
			}
		}
	} else if (sdc->state == SDC_WAIT) {		// waiting for command arguments
		sdc->arg[6 - sdc->argCnt] = val & 0xff;
		sdc->argCnt--;
		if (sdc->argCnt == 0) {
//			printf("SD exec: %.2X:%.2X:%.2X:%.2X:%.2X:%.2X\n",sdc->arg[0],sdc->arg[1],sdc->arg[2],sdc->arg[3],sdc->arg[4],sdc->arg[5]);
			sdc->state = SDC_FREE;
			sdcExec(sdc);
			sdc->argCnt = 6;
		}
	} else if ((sdc->state == SDC_FREE) || (val == (CMD12 | 0x40))) {	// waiting for command | block operation break command
		if ((val & 0xc0) == 0x40) {			// %01xxxxxx accepted as a command token
			sdc->arg[0] = val & 0xff;
			sdc->argCnt = 5;
			sdc->state = SDC_WAIT;
		}
	}
}

void sdcOpenFile(SDCard* sdc) {
	sdcCloseFile(sdc);
	if (sdc->image) {
		sdc->file = fopen(sdc->image,"rb+");
		if (sdc->file) {
			x_fseek(sdc->file, 0, SEEK_END);
			long long sz = x_ftell(sdc->file);
			long long sz2 = 256;
			while (sz2 < sz)
				sz2 <<= 1;
			sdc->capacity = sz2 >> 20;	// MegaBytes
			sdc->maxlba = sz2 >> 9;		// 512-byte sectors
			sdc_set_kind(sdc);
		}
	}
}

void sdcCloseFile(SDCard* sdc) {
	if (sdc->file)
		fclose(sdc->file);
	sdc->file = NULL;
	if (sdc->vfat)
		vfat_free(sdc->vfat);
	sdc->vfat = NULL;
}

// A card on an SPI bus, as the SD specification (physical layer, part 1, ch. 7)
// has it. Every transfer is a byte each way: what the card says goes out while
// the host's byte comes in. A host clocks FF to read, and the card says FF when
// it has nothing to say.
//
// Times are counted in transfers. The card's own delays are its own, the
// specification gives ranges only, so a plain mid-range card is taken:
#define	SDS_NCR		1	// FF before an R1: 1..8 bytes
#define	SDS_NAC		8	// FF before a data token on a read: up to 100 ms on a slow card
#define	SDS_BUSY	100	// 00 after a written block: about 1 ms at a Spectrum's pace

// CRC7 of a command or a register, shifted with its end bit
static int sds_crc7(const unsigned char* p, int n) {
	int crc = 0;
	for (int i = 0; i < n; i++) {
		int b = p[i];
		for (int k = 0; k < 8; k++) {
			crc <<= 1;
			if ((b ^ crc) & 0x80) crc ^= 0x09;
			b <<= 1;
		}
		crc &= 0x7f;
	}
	return (crc << 1) | 1;
}

// CRC16-CCITT of a data block
static int sds_crc16(const unsigned char* p, int n) {
	int crc = 0;
	for (int i = 0; i < n; i++) {
		crc ^= p[i] << 8;
		for (int k = 0; k < 8; k++)
			crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
		crc &= 0xffff;
	}
	return crc;
}

SDCard* sdc_create_spec(void) {
	SDCard* sdc = sdcCreate();
	if (!sdc) return NULL;
	sdc->spec = 1;
	sdc_power_on(sdc);
	return sdc;
}

void sdc_power_on(SDCard* sdc) {
	sdc_idle(sdc);
	sdc->spimode = 0;
	sdc->checkCrc = 0;
	sdc->blkSize = 512;
	sdc->out.pos = sdc->out.len = 0;
	sdc->busyLeft = 0;
	sdc->argCnt = 6;
}

static void sds_put(SDCard* sdc, int val) {
	if (sdc->out.len < (int)sizeof(sdc->out.data))
		sdc->out.data[sdc->out.len++] = val & 0xff;
}

static void sds_gap(SDCard* sdc, int n) {
	while (n-- > 0) sds_put(sdc, 0xff);
}

static int sds_r1(SDCard* sdc, int flags) {
	return (flags | (sdc->idle ? R1_IDLE : 0)) & 0xff;
}

// a new answer: what was left of the last one is dropped
static void sds_answer(SDCard* sdc, int r1) {
	sdc->out.pos = sdc->out.len = 0;
	sds_gap(sdc, SDS_NCR);
	sds_put(sdc, r1);
}

// a data token, the bytes and their CRC16
static void sds_block(SDCard* sdc, const unsigned char* p, int n) {
	sds_gap(sdc, SDS_NAC);
	sds_put(sdc, 0xfe);
	for (int i = 0; i < n; i++) sds_put(sdc, p[i]);
	int crc = sds_crc16(p, n);
	sds_put(sdc, crc >> 8);
	sds_put(sdc, crc);
}

// the sectors the card holds
static unsigned int sds_sectors(SDCard* sdc) {
	return sdc->maxlba;
}

// CSD: version 2.0 for an SDHC card, 1.0 for an SDSC one, sized to the medium
static void sds_csd(SDCard* sdc, unsigned char* p) {
	memset(p, 0, 16);
	unsigned int sec = sds_sectors(sdc);
	if (sdc->sdhc) {
		unsigned int cs = (sec >> 10) ? (sec >> 10) - 1 : 0;
		p[0] = 0x40;		// CSD_STRUCTURE 1
		p[1] = 0x0e;		// TAAC 1 ms
		p[2] = 0x00;		// NSAC
		p[3] = 0x32;		// TRAN_SPEED 25 MHz
		p[4] = 0x5b;		// CCC 010110110101
		p[5] = 0x59;		// ... READ_BL_LEN 9
		p[6] = 0x00;
		p[7] = (cs >> 16) & 0x3f;
		p[8] = (cs >> 8) & 0xff;
		p[9] = cs & 0xff;
		p[10] = 0x7f;		// ERASE_BLK_EN, SECTOR_SIZE 7F
		p[11] = 0x80;
		p[12] = 0x0a;		// R2W_FACTOR 2, WRITE_BL_LEN 9
		p[13] = 0x40;
		p[14] = 0x00;
	} else {
		// capacity = (C_SIZE + 1) * 2^(C_SIZE_MULT + 2) * 2^READ_BL_LEN
		int bl = (sec > 0x200000) ? 10 : 9;	// a 2 GB card counts 1K blocks
		unsigned int blocks = sec >> (bl - 9);
		int m = 0;
		while ((m < 7) && ((blocks >> (m + 2)) > 4096)) m++;
		unsigned int cs = (blocks >> (m + 2)) ? (blocks >> (m + 2)) - 1 : 0;
		if (cs > 4095) cs = 4095;
		p[0] = 0x00;		// CSD_STRUCTURE 0
		p[1] = 0x26;		// TAAC
		p[2] = 0x00;
		p[3] = 0x32;
		p[4] = 0x5f;		// CCC
		p[5] = 0x50 | bl;
		p[6] = 0x80 | ((cs >> 10) & 0x03);	// READ_BL_PARTIAL
		p[7] = (cs >> 2) & 0xff;
		p[8] = ((cs & 0x03) << 6) | 0x3f;	// VDD_R_CURR_MIN/MAX
		p[9] = 0xfc | ((m >> 1) & 0x03);	// VDD_W_CURR_MIN/MAX
		p[10] = ((m & 1) << 7) | 0x7f;		// ERASE_BLK_EN, SECTOR_SIZE
		p[11] = 0x80;
		p[12] = 0x08 | (bl >> 2);		// R2W_FACTOR 2
		p[13] = (bl & 3) << 6;
		p[14] = 0x00;
	}
	p[15] = sds_crc7(p, 15);
}

// CID: a card of no maker in particular
static void sds_cid(SDCard* sdc, unsigned char* p) {
	static const unsigned char cid[15] = {0x00, 'X', 'P', 'X', 'P', 'C', 'R', 'D', 0x10, 0x00, 0x00, 0x00, 0x01, 0x01, 0x9a};
	(void)sdc;
	memcpy(p, cid, 15);
	p[15] = sds_crc7(p, 15);
}

// the sector a read or a write command names: a byte address on an SDSC card
static unsigned int sds_lba(SDCard* sdc) {
	unsigned int arg = sdcGetArg(sdc, 0xffffffff);
	return sdc->sdhc ? arg : (arg >> 9);
}

static void sds_read_block(SDCard* sdc) {
	sdcRdSector(sdc);
	sds_block(sdc, sdc->buf.data + 1, 512);
}

static void sds_exec(SDCard* sdc) {
	int cmd = sdc->arg[0] & 0x3f;
	int crc_ok = (sds_crc7(sdc->arg, 5) == sdc->arg[5]);
	unsigned char reg[16];
	if (!sdc->spimode) {
		// SD mode: only a CMD0 with its CRC is heard, and it puts the card in SPI mode
		if ((cmd != CMD00) || !crc_ok) return;
		sdc->spimode = 1;
	}
	// CMD0 and CMD8 are always checked, the rest once CMD59 asks for it
	if (!crc_ok && (sdc->checkCrc || (cmd == CMD00) || (cmd == CMD08)) && !sdc->acmd) {
		sds_answer(sdc, sds_r1(sdc, R1_CRC_ERROR));
		return;
	}
	if (sdc->acmd) {
		sdc->acmd = 0;
		switch (cmd) {
			case ACMD41:
				// an SDHC card stays idle for a host that does not say it knows them
				if (!sdc->sdhc || (sdc->arg[1] & 0x40))
					sdc->idle = 0;
				sds_answer(sdc, sds_r1(sdc, 0));
				break;
			default:
				sds_answer(sdc, sds_r1(sdc, R1_ILLEGAL));
				break;
		}
		return;
	}
	unsigned int lba;
	switch (cmd) {
		case CMD00:
			sdc_idle(sdc);
			sds_answer(sdc, sds_r1(sdc, 0));
			break;
		case CMD01:			// the MMC way out of idle, which an SD card takes too
			sdc->idle = 0;
			sds_answer(sdc, sds_r1(sdc, 0));
			break;
		case CMD08:			// R7: the voltage accepted and the check pattern back
			sds_answer(sdc, sds_r1(sdc, 0));
			sds_put(sdc, 0x00);
			sds_put(sdc, 0x00);
			sds_put(sdc, sdc->arg[3] & 0x0f);
			sds_put(sdc, sdc->arg[4]);
			break;
		case CMD09:
		case CMD10:
			sds_answer(sdc, sds_r1(sdc, 0));
			if (cmd == CMD09) sds_csd(sdc, reg); else sds_cid(sdc, reg);
			sds_block(sdc, reg, 16);
			break;
		case CMD12:			// a stuff byte and R1; a read stops at once, with no busy after it
			sdc->cont = 0;		// (a host may send its next command right after the R1)
			sdc->state = SDC_FREE;
			sdc->out.pos = sdc->out.len = 0;
			sds_put(sdc, 0xff);
			sds_gap(sdc, SDS_NCR);
			sds_put(sdc, sds_r1(sdc, 0));
			break;
		case CMD13:			// R2: nothing wrong
			sds_answer(sdc, sds_r1(sdc, 0));
			sds_put(sdc, 0x00);
			break;
		case CMD16:
			if (!sdc->sdhc) {
				unsigned int len = sdcGetArg(sdc, 0xffffffff);
				if ((len == 0) || (len > 512)) {
					sds_answer(sdc, sds_r1(sdc, R1_PARAM_ERR));
					break;
				}
				sdc->blkSize = len;
			}
			sds_answer(sdc, sds_r1(sdc, 0));
			break;
		case CMD17:
		case CMD18:
			lba = sds_lba(sdc);
			if (sdc->idle || (lba >= sdc->maxlba)) {
				sds_answer(sdc, sds_r1(sdc, sdc->idle ? R1_ILLEGAL : R1_PARAM_ERR));
				break;
			}
			sdc->addr = lba;
			sdc->cont = (cmd == CMD18);
			sdc->reads++;
			sdc->state = SDC_READ;		// no command heard until the block is out
			sds_answer(sdc, sds_r1(sdc, 0));
			sds_read_block(sdc);
			break;
		case CMD24:
		case CMD25:
			lba = sds_lba(sdc);
			if (sdc->idle || (lba >= sdc->maxlba)) {
				sds_answer(sdc, sds_r1(sdc, sdc->idle ? R1_ILLEGAL : R1_PARAM_ERR));
				break;
			}
			sdc->addr = lba;
			sdc->cont = (cmd == CMD25);
			sdc->state = SDC_WRITE;
			sdc->buf.pos = -1;		// waiting for the token
			sds_answer(sdc, sds_r1(sdc, 0));
			break;
		case CMD55:
			sdc->acmd = 1;
			sds_answer(sdc, sds_r1(sdc, 0));
			break;
		case CMD58:			// R3: OCR, the power-up done bit once out of idle, CCS
			sds_answer(sdc, sds_r1(sdc, 0));
			sds_put(sdc, sdc->idle ? 0x00 : (0x80 | (sdc->sdhc ? 0x40 : 0x00)));
			sds_put(sdc, 0xff);
			sds_put(sdc, 0x80);
			sds_put(sdc, 0x00);
			break;
		case CMD59:
			sdc->checkCrc = sdc->arg[4] & 1;
			sds_answer(sdc, sds_r1(sdc, 0));
			break;
		default:
			sds_answer(sdc, sds_r1(sdc, R1_ILLEGAL));
			break;
	}
}

// a byte of a block being written
static void sds_write_byte(SDCard* sdc, int val) {
	if (sdc->buf.pos < 0) {			// waiting for the token
		if (val == 0xff) return;
		if (sdc->cont && (val == 0xfd)) {	// stop: busy, then free
			sdc->state = SDC_FREE;
			sdc->cont = 0;
			sdc->out.pos = sdc->out.len = 0;
			sds_put(sdc, 0xff);
			sdc->busyLeft = SDS_BUSY;
			return;
		}
		if (val == (sdc->cont ? 0xfc : 0xfe))
			sdc->buf.pos = 0;
		return;
	}
	sdc->buf.data[1 + sdc->buf.pos] = val & 0xff;
	sdc->buf.pos++;
	if (sdc->buf.pos < 514) return;		// 512 data and 2 crc
	int resp;
	if (sdc->checkCrc && (sds_crc16(sdc->buf.data + 1, 512) != ((sdc->buf.data[513] << 8) | sdc->buf.data[514]))) {
		resp = 0x0b;			// crc error
	} else if (sdc_protected(sdc) || !sdcWrSector(sdc)) {
		resp = 0x0d;			// write error
	} else {
		resp = 0x05;
	}
	sdc->out.pos = sdc->out.len = 0;
	sds_put(sdc, resp);
	sdc->busyLeft = (resp == 0x05) ? SDS_BUSY : 0;
	sdc->buf.pos = -1;
	if ((resp == 0x05) && sdc->cont) {
		sdc->addr++;
		if (sdc->addr >= sdc->maxlba) sdc->cont = 0;
	}
	if (!sdc->cont) sdc->state = SDC_FREE;
}

static int sds_next_out(SDCard* sdc) {
	if (sdc->out.pos < sdc->out.len) {
		int val = sdc->out.data[sdc->out.pos++];
		if ((sdc->out.pos == sdc->out.len) && (sdc->state == SDC_READ) && !sdc->cont)
			sdc->state = SDC_FREE;		// a single block read is over
		return val;
	}
	if (sdc->busyLeft > 0) {
		sdc->busyLeft--;
		return 0x00;
	}
	if ((sdc->state == SDC_READ) && sdc->cont) {	// CMD18: the next block, until CMD12
		sdc->addr++;
		if (sdc->addr >= sdc->maxlba) {
			sdc->cont = 0;
			sdc->state = SDC_FREE;
			return 0xff;
		}
		sdc->out.pos = sdc->out.len = 0;
		sds_read_block(sdc);
		return sdc->out.data[sdc->out.pos++];
	}
	return 0xff;
}

int sdc_xfer(SDCard* sdc, int mosi) {
	if (!sdc || !sdc->image || sdc->cs) return 0xff;
	mosi &= 0xff;
	int busy = (sdc->busyLeft > 0);
	int miso = sds_next_out(sdc);
	if (busy && (sdc->out.pos >= sdc->out.len)) {
		// programming: nothing it is sent is heard
	} else if (sdc->state == SDC_WRITE) {
		sds_write_byte(sdc, mosi);
	} else if (sdc->state == SDC_WAIT) {
		sdc->arg[6 - sdc->argCnt] = mosi;
		if (--sdc->argCnt == 0) {
			sdc->argCnt = 6;
			sdc->state = SDC_FREE;
			sds_exec(sdc);
		}
	} else if (((sdc->state == SDC_FREE) || (mosi == (0x40 | CMD12))) && ((mosi & 0xc0) == 0x40)) {
		// a command starts: a byte 01xxxxxx while the card takes commands, or
		// CMD12 in the middle of a multi-block read
		sdc->arg[0] = mosi;
		sdc->argCnt = 5;
		sdc->state = SDC_WAIT;
	}
	return miso;
}
