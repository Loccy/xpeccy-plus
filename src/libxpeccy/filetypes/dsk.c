#include "filetypes.h"

// +0x00 signature (23)
// +0x17 DiskInfo\r\n
// +0x22 disk info block

#pragma pack (push, 1)

typedef struct {
	char byte0d0f[4];	// unused
	unsigned char track;
	unsigned char side;
	unsigned char dataRate;	// unknown, SD, HD, ED
	unsigned char recMode;	// unknown, FM, MFM
	unsigned char secSize;
	unsigned char secCount;
	unsigned char gap3Size;
	unsigned char filler;
	char sectorInfo[0x100 - 0x18];	// max 29 (x 8 bytes)
} TrackInfBlock;

typedef struct {
	unsigned char track;
	unsigned char side;
	unsigned char sector;
	unsigned char size;
	unsigned char sr1;
	unsigned char sr2;
	unsigned char bslo;
	unsigned char bshi;
} SectorInfBlock;	// 8 bytes

#pragma pack (pop)

//static const char edsksgn[] =	"EXTENDED CPC DSK File\r\n";
//static const char dsksgn[] =	"MV - CPCEMU Disk-File\r\n";
static const char edsksgn[] =	"EXTENDED";
static const char dsksgn[] =	"MV - CPC";

// A sector as the image gives it
typedef struct {
	unsigned char c, h, r, n;
	unsigned char st1, st2;	// what the dumping controller answered reading it
	int len;		// bytes of one read
	int copies;		// reads kept: more than one is a weak sector
	const unsigned char* data;	// copies x len, in the file's buffer
} dskSec;

// The track as the uPD765's Format lays it out in MFM: gap 4a, the index mark,
// gap 1, then for each sector sync and ID, gap 2, sync and data, gap 3
#define DSK_GAP4A	80
#define DSK_GAP1	50
#define DSK_GAP2	22
#define DSK_SYNC	12
#define DSK_GAP3_P3	0x52	// gap 3 +3DOS formats with: its format spec, +3 ROM 2 at 1C5C
#define DSK_LEAD	(DSK_GAP4A + DSK_SYNC + 4 + DSK_GAP1)
#define DSK_SEC_FIX	(DSK_SYNC + 4 + 4 + 2 + DSK_GAP2 + DSK_SYNC + 4 + 2)	// a sector but its data and gap 3

// what a size code means to the controller
static int dsk_nominal(int n) {
	return (n > 7) ? 0x4000 : (0x80 << n);
}

typedef struct {
	flpTrack* t;
	int pos;
	int len;
} dskPen;

static void pen_put(dskPen* p, unsigned char b, unsigned char fld) {
	if (p->pos >= p->len) return;		// past the index: the rest is not on the track
	p->t->byte[p->pos] = b;
	p->t->field[p->pos] = fld;
	p->pos++;
}

static void pen_run(dskPen* p, unsigned char b, int n) {
	while (n-- > 0) pen_put(p, b, 0);
}

// sync, A1 A1 A1 and the mark; returns where its CRC starts counting
static int pen_mark(dskPen* p, unsigned char mark) {
	pen_run(p, 0x00, DSK_SYNC);
	int st = p->pos;
	pen_put(p, 0xa1, 0x80);
	pen_put(p, 0xa1, 0x0f);
	pen_put(p, 0xa1, 0x0f);
	pen_put(p, mark, 0x0f);
	return st;
}

// the CRC of what is laid down from st, here; bad: written wrong, as the image says
static void pen_crc(dskPen* p, int st, int bad) {
	if (p->pos + 2 > p->len) {		// the field runs off the track: no CRC on it
		p->pos = p->len;
		return;
	}
	unsigned short crc = getCrc(p->t->byte + st, p->pos - st);
	if (bad) crc ^= 0xffff;
	pen_put(p, (crc >> 8) & 0xff, 4);
	pen_put(p, crc & 0xff, 4);
}

// Lay a track out from its sectors. What does not fit takes gap 3 first, then the
// lead-in; a data field longer than the room left (N 6 and up) runs to the index
// with no CRC after it, as the Format command leaves one.
static void dsk_form(Floppy* flp, int tr, dskSec* s, int n, int gap3) {
	if ((tr < 0) || (tr > 255) || (n < 1)) return;
	dskPen p = {&flp->data[tr], 0, flp->trklen};
	memset(p.t->byte, 0x4e, TRKLEN_HD);
	memset(p.t->field, 0, TRKLEN_HD);
	memset(p.t->map, 0, sizeof(p.t->map));
	long need = 0;
	for (int i = 0; i < n; i++)
		need += DSK_SEC_FIX + s[i].len;
	int lead = DSK_LEAD;
	if (lead + need + (long)n * gap3 > p.len) {
		long g = (p.len - lead - need) / n;
		gap3 = (g > 0) ? (int)g : 0;
		if (lead + need > p.len) lead = 0;
	}
	if (lead) {
		pen_run(&p, 0x4e, DSK_GAP4A);
		pen_run(&p, 0x00, DSK_SYNC);
		pen_run(&p, 0xc2, 3);
		pen_run(&p, 0xfc, 1);
		pen_run(&p, 0x4e, DSK_GAP1);
	}
	for (int i = 0; (i < n) && (p.pos < p.len); i++) {
		dskSec* x = &s[i];
		int st = pen_mark(&p, 0xfe);
		pen_put(&p, x->c, 1);
		pen_put(&p, x->h, 1);
		pen_put(&p, x->r, 1);
		pen_put(&p, x->n, 1);
		pen_crc(&p, st, (x->st1 & 0x20) && !(x->st2 & 0x20));	// a CRC error in the ID
		pen_run(&p, 0x4e, DSK_GAP2);
		if (!(x->st2 & 0x01)) {				// no MD: there is a data field
			int del = (x->st2 & 0x40) ? 1 : 0;
			st = pen_mark(&p, del ? 0xf8 : 0xfb);
			int dpos = p.pos;
			if ((dpos < p.len) && !p.t->map[x->r])
				p.t->map[x->r] = dpos;
			for (int k = 0; k < x->len; k++)
				pen_put(&p, x->data[k], del ? 3 : 2);
			if ((x->copies > 1) && (dpos + x->len <= p.len))
				flp_add_weak(flp, tr, dpos, x->len, x->copies, x->data);
			if (x->len >= dsk_nominal(x->n))
				pen_crc(&p, st, x->st2 & 0x20);		// a CRC error in the data
		}
		pen_run(&p, 0x4e, gap3);
	}
}

static unsigned char* dsk_file(const char* name, long* len) {
	FILE* file = fopen(name, "rb");
	if (!file) return NULL;
	unsigned char* buf = NULL;
	if (!fseek(file, 0, SEEK_END)) {
		long sz = ftell(file);
		if ((sz > 0) && (sz < 0x4000000) && !fseek(file, 0, SEEK_SET)) {
			buf = (unsigned char*)malloc(sz);
			if (buf && (fread(buf, 1, sz, file) != (size_t)sz)) {
				free(buf);
				buf = NULL;
			}
			*len = sz;
		}
	}
	fclose(file);
	return buf;
}

int dsk_read(Floppy* flp, const char* name) {
	long len = 0;
	unsigned char* buf = dsk_file(name, &len);
	if (!buf) return ERR_CANT_OPEN;
	int ext = -1;
	if (len < 0x100) {
		ext = -1;
	} else if (!strncmp((char*)buf, edsksgn, strlen(edsksgn))) {
		ext = 1;
	} else if (!strncmp((char*)buf, dsksgn, strlen(dsksgn))) {
		ext = 0;
	}
	if (ext < 0) {
		free(buf);
		return ERR_DSK_SIGN;
	}
	int err = ERR_OK;
	diskClear(flp);
	int trkcnt = buf[0x30];
	int sidcnt = buf[0x31];
	int tlen = buf[0x32] | (buf[0x33] << 8);
	int tracks = trkcnt * sidcnt;
	if (ext && (tracks > 0x100 - 0x34)) tracks = 0x100 - 0x34;	// all the size table holds
	long pos = 0x100;
	int tr = 0;
	int cut = 0;
	dskSec secs[29];
	for (int i = 0; (i < tracks) && !cut; i++) {
		long rtlen = ext ? (buf[0x34 + i] << 8) : tlen;
		if (rtlen > 0) {
			if (pos + 0x100 > len) break;			// the file ends: keep what is loaded
			if (strncmp((char*)buf + pos, "Track-Info\r\n", 12)) {
				err = ERR_DSK_SIGN;
				break;
			}
			TrackInfBlock* tib = (TrackInfBlock*)(buf + pos + 12);
			SectorInfBlock* sib = (SectorInfBlock*)tib->sectorInfo;
			int nsec = tib->secCount;
			if (nsec > 29) nsec = 29;			// all the sector info there is
			long dp = pos + 0x100;
			int sc;
			for (sc = 0; (sc < nsec) && !cut; sc++) {
				dskSec* x = &secs[sc];
				x->c = sib[sc].track;
				x->h = sib[sc].side;
				x->r = sib[sc].sector;
				x->n = sib[sc].size;
				x->st1 = sib[sc].sr1;
				x->st2 = sib[sc].sr2;
				int nom = dsk_nominal(x->n);
				long slen;
				if (ext) {
					slen = sib[sc].bslo | (sib[sc].bshi << 8);
				} else {					// the track's size code stands for every sector
					slen = (tib->secSize > 5) ? 0x1800 : (0x80 << tib->secSize);
				}
				if (dp + slen > len) {				// the file ends inside this sector
					slen = (len > dp) ? len - dp : 0;
					cut = 1;
				}
				x->data = buf + dp;
				x->copies = 1;
				x->len = (int)slen;
				if (ext && (slen > nom) && !(slen % nom)) {	// several reads of a weak sector
					x->copies = (int)(slen / nom);
					x->len = nom;
				} else if (slen > nom) {
					x->len = nom;
				}
				dp += slen;
			}
			// 4E is what the dumping tools put there for any track, not a length
			// they measured: such a track gets the gap +3DOS formats with
			int gap3 = (tib->gap3Size == 0x4e) ? DSK_GAP3_P3 : tib->gap3Size;
			dsk_form(flp, tr, secs, sc, gap3);
			tr += (sidcnt == 1) ? 2 : 1;
			pos += rtlen;
		} else {
			tr += (sidcnt == 1) ? 2 : 1;
		}
	}
	free(buf);
	flp_insert(flp, name);		// what was read up to a broken track goes in, as before
	return err;
}

int loadDSK(Computer* comp, const char *name, int drv) {
	return dsk_read(comp->dif->flp[drv & 3], name);
}

// the image built in memory, grown as it goes
typedef struct {
	unsigned char* data;
	size_t len;
	size_t cap;
	int fail;
} dskBuf;

static void db_put(dskBuf* b, const void* src, size_t n) {
	if (b->fail) return;
	if (b->len + n > b->cap) {
		size_t cap = b->cap ? b->cap : 0x10000;
		while (cap < b->len + n) cap <<= 1;
		unsigned char* nd = (unsigned char*)realloc(b->data, cap);
		if (!nd) {
			b->fail = 1;
			return;
		}
		b->data = nd;
		b->cap = cap;
	}
	memcpy(b->data + b->len, src, n);
	b->len += n;
}

static size_t dsk_save_track(Floppy* flp, int trk, int side, dskBuf* b) {
	TrackInfBlock tinf;
	Sector sdata[29];
	size_t fpos = b->len;
	int sec = 0;
	int pos = 0;
	int i, dsz, dps;
	int mdsz = 0;
	int rtrk = (trk << 1) | side;
	side &= 1;
	memset(&tinf, 0, sizeof(TrackInfBlock));
	// scan track & collect sectors info & data
	for (pos = 0; (pos < flp->trklen) && (sec < 29); pos++) {
		if (flp->data[rtrk].field[pos] == 1) {			// sector info
			sdata[sec].trk = flp->data[rtrk].byte[pos++];	// C
			sdata[sec].head = flp->data[rtrk].byte[pos++];	// H
			sdata[sec].sec = flp->data[rtrk].byte[pos++];	// R
			sdata[sec].sz = flp->data[rtrk].byte[pos++] & 7;// N
			pos += 2;					// skip crc
			if (sdata[sec].sz > mdsz)			// max N
				mdsz = sdata[sec].sz;
			dsz = 128 << sdata[sec].sz;			// sector data size
			if (dsz > 0x1800)
				dsz = 0x1800;
			dps = flp->data[rtrk].map[sdata[sec].sec];	// position of sector data
			if (dps > 0)
				sdata[sec].type = flp->data[rtrk].byte[dps - 1];	// sector data type (FB | F8)
			memcpy(sdata[sec].data, flp->data[rtrk].byte + dps, dsz);	// copy sector data
			sec++;
		}
	}
	if (sec < 1)		// no sectors found, no data written, size = 0
		return 0;
	// fill track info
	tinf.track = trk & 0xff;
	tinf.side = side & 1;
	tinf.secSize = mdsz & 7;		// 0 @ extend dsk ?
	tinf.secCount = sec & 0xff;
	tinf.gap3Size = 0x1a;
	tinf.filler = 0xe5;
	pos = 0;
	for (i = 0; i < sec; i++) {
		tinf.sectorInfo[pos] = sdata[i].trk;
		tinf.sectorInfo[pos + 1] = sdata[i].head;
		tinf.sectorInfo[pos + 2] = sdata[i].sec;
		tinf.sectorInfo[pos + 3] = sdata[i].sz;
		dsz = 128 << sdata[i].sz;
		if (i == (sec - 1))				// end of cylinder
			tinf.sectorInfo[pos + 4] = 0x80;
		tinf.sectorInfo[pos + 6] = dsz & 0xff;
		tinf.sectorInfo[pos + 7] = (dsz >> 8) & 0xff;
		pos += 8;
	}
	// save track info block
	db_put(b, "Track-Info\r\n", 12);
	db_put(b, &tinf, sizeof(TrackInfBlock));
	// save sectors data
	for (i = 0; i < sec; i++) {
		dsz = (128 << sdata[i].sz);
		if (dsz > 0x1800)
			dsz = 0x1800;
		db_put(b, sdata[i].data, dsz);
	}
	return b->len - fpos;
}

static const char sign[] = "EXTENDED CPC DSK File\r\nDisk-Info\r\nXpeccy        ";

unsigned char* dsk_build(Floppy* flp, size_t* len) {
	dskBuf b;
	unsigned char buf[256];
	memset(&b, 0, sizeof(b));
	memset(buf, 0, 0x100);
	memcpy(buf, sign, 0x22 + 0x0e);
	// 30 = track count, will be updated later
	// 31: 1 | 2 sides
	// 32,33 = 0 @ extend format
	// 34+N = size of track N (from Track-Info to Track-Info)
	db_put(&b, buf, 256);		// signature + disk info: filled in at the end
	int cpos = 0x34;
	size_t isize;
	int ds = 0;
	int tcount = 0;
	for (int i = 0; i < 86; i++) {
		isize = dsk_save_track(flp, i, 0, &b);
		buf[cpos] = (isize >> 8) & 0xff;
		cpos++;
		if (isize > 0)
			tcount = i + 1;
		if (flp->doubleSide) {
			isize = dsk_save_track(flp, i, 1, &b);
			if (isize > 0)
				ds = 1;
			buf[cpos] = (isize >> 8) & 0xff;
			cpos++;
			if (isize > 0)
				tcount = i + 1;
		}
	}
	buf[0x30] = (tcount < 43) ? 42 : 84; // = tcount & 0xff;
	buf[0x31] = ds ? 2 : 1;
	if (!ds) {
		cpos = 0x36;		// 0x34 doesn't changed
		tcount = 0x35;
		while(cpos < 256) {
			buf[tcount] = buf[cpos];
			buf[cpos] = 0;
			tcount++;
			cpos += 2;
		}
	}
	if (b.fail) {
		free(b.data);
		return NULL;
	}
	memcpy(b.data, buf, 256);
	*len = b.len;
	return b.data;
}

int saveDSK(Computer* comp, const char* name, int drv) {
	Floppy* flp = comp->dif->flp[drv & 3];
	size_t len = 0;
	unsigned char* img = dsk_build(flp, &len);
	if (!img) return ERR_CANT_OPEN;
	FILE* file = fopen(name, "wb");
	int err = ERR_CANT_OPEN;
	if (file) {
		if (fwrite(img, 1, len, file) == len)
			err = ERR_OK;
		fclose(file);
	}
	free(img);
	if (err == ERR_OK) {
		// update name
		flp_set_path(flp, name);
		flp->changed = 0;
	}
	return err;
}
