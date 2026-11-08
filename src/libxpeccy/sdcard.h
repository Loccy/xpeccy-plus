#pragma once

#include <stdio.h>

#include "vfat.h"
#include "defines.h"

/*
// mode
enum {
	SDC_INACTIVE = 0,
	SDC_IDLE,
};
*/
// state
enum {
	SDC_FREE = 0,		// ready to accept a command
	SDC_WAIT,		// waiting for command arguments
	SDC_READ,		// send data to CPU
	SDC_WRITE		// accept data from CPU
};

// R1 flags
#define	R1_IDLE		1
#define	R1_ERASE_RESET	(1<<1)
#define	R1_ILLEGAL	(1<<2)
#define	R1_CRC_ERROR	(1<<3)
#define	R1_ERASE_SEQ	(1<<4)
#define	R1_ADDRESS_ERR	(1<<5)
#define	R1_PARAM_ERR	(1<<6)
typedef struct {
	unsigned on:1;
	unsigned cs:1;
	unsigned acmd:1;
	unsigned checkCrc:1;
	unsigned cont:1;
	unsigned lock:1;
	unsigned busy:1;
	unsigned idle:1;	// in idle state: every R1 carries the bit until ACMD41
	// A card on a real SPI bus (sdc_xfer): every byte in is a byte out, a command it
	// does not know is answered as illegal, and a card up to 2 GB is an SDSC one with
	// byte addresses. Without it, the card the Z-controller ports have always had.
	unsigned spec:1;
	unsigned sdhc:1;	// block addresses, CCS set in the OCR (spec cards: by size)
	unsigned spimode:1;	// spec cards: a CMD0 with /CS low has put it in SPI mode

//	unsigned char mode;	// page 18 of SDCard specification 3.01
	unsigned char state;	// current action

	unsigned char argCnt;	// arguments countdown for command (4 + 1)
	unsigned char arg[6];	// input block (command, 4 arguments, crc)

	unsigned char respCnt;	// size of response in bytes
	unsigned char respPos;	// current response byte
	unsigned char resp[17];	// response (max 136 bits @ R2)

	unsigned int blkSize;
	unsigned int addr;
	int capacity;
	unsigned int maxlba;
	char* image;		// image file name, or the mounted host folder
	FILE* file;		// image file
	vFat* vfat;		// synthetic volume, when a folder is mounted
	struct {		// data buffer
		int pos;
		unsigned char data[515];	// data packet: token(1),data(512),crc(2)
	} buf;
	// spec cards: what goes out on MISO next, one byte per transfer
	struct {
		int pos;
		int len;
		unsigned char data[530];	// gap, R1, gap, token, a block or a register, crc
	} out;
	int busyLeft;		// transfers still answered 00 after a block written
	int reads;		// blocks read since the card was made: a card in use moves it
	int writes;		// blocks the machine wrote, kept or not (a folder is read only)
} SDCard;

SDCard* sdcCreate();
void sdcDestroy(SDCard*);
void sdcReset(SDCard*);

int sdcRead(SDCard*);
void sdcWrite(SDCard*, int);
// spec cards: one byte each way on the bus, MOSI in and MISO back; FF while the
// card is not selected (/CS high, sdc->cs = 1) or not there
int sdc_xfer(SDCard*, int);
SDCard* sdc_create_spec(void);
void sdc_power_on(SDCard*);	// spec cards: the supply comes up, back in SD mode and idle

void sdcSetImage(SDCard*,const char*);
void sdcSetFolder(SDCard*,const char*,vFat*);
void sdcSetUnread(SDCard*,const char*);	// a folder's path kept, nothing read: it reads as no card
int sdc_unread(SDCard*);			// ...and is one such
int sdc_protected(SDCard*);
void sdcSetLock(SDCard*,int);
int sdc_read_lba(SDCard*, unsigned int lba, unsigned char* buf);	// a sector outside any transfer: 1 if the card has it
int sdc_has_file(SDCard*, const char* path);	// a FAT volume on the card holds this file ("dir/name.ext")
unsigned long long sdc_dir_print(SDCard*);	// what it holds as the guest reads it (fat_dir_print), 0 none

void sdcOpenFile(SDCard*);
void sdcCloseFile(SDCard*);
