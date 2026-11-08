#include <string.h>
#include <ctype.h>

#include "fatfile.h"

typedef struct {
	fatSector rd;
	void* ctx;
	unsigned int base;		// first sector of the volume
	unsigned int spc;		// sectors per cluster
	unsigned int fatStart;		// first FAT, relative to the volume
	unsigned int rootStart;		// FAT16: the fixed root directory...
	unsigned int rootSecs;		// ...and its length
	unsigned int rootClust;		// FAT32: the root's first cluster
	unsigned int dataStart;		// cluster 2
	unsigned int clusters;
	int fat32;
} fatVol;

typedef struct {
	unsigned int clust;		// first cluster, 0 for the FAT16 root
	unsigned int size;
	int isdir;
} fatEnt;

static unsigned int le16(const unsigned char* p) {return p[0] | (p[1] << 8);}
static unsigned int le32(const unsigned char* p) {return le16(p) | (le16(p + 2) << 16);}

static int fat_bpb(fatVol* v, const unsigned char* s) {
	if ((le16(s + 11) != 512) || !s[13] || (s[13] & (s[13] - 1)) || !s[16]) return 0;
	unsigned int rsv = le16(s + 14);
	unsigned int fatsz = le16(s + 22) ? le16(s + 22) : le32(s + 36);
	unsigned int tot = le16(s + 19) ? le16(s + 19) : le32(s + 32);
	unsigned int rootSecs = (le16(s + 17) * 32 + 511) / 512;
	if (!rsv || !fatsz) return 0;
	v->spc = s[13];
	v->fatStart = rsv;
	v->rootStart = rsv + s[16] * fatsz;
	v->rootSecs = rootSecs;
	v->dataStart = v->rootStart + rootSecs;
	if (tot <= v->dataStart) return 0;
	v->clusters = (tot - v->dataStart) / v->spc;
	if (v->clusters <= 4085) return 0;	// FAT12: not on a card
	v->fat32 = (v->clusters > 65525);
	v->rootClust = v->fat32 ? le32(s + 44) : 0;
	return 1;
}

static int fat_open(fatVol* v, unsigned char* s) {
	if (!v->rd(v->ctx, 0, s) || (s[510] != 0x55) || (s[511] != 0xaa)) return 0;
	v->base = 0;
	if (((s[0] == 0xeb) || (s[0] == 0xe9)) && fat_bpb(v, s)) return 1;
	unsigned int start[4];
	for (int i = 0; i < 4; i++) start[i] = s[0x1be + i * 16 + 4] ? le32(s + 0x1be + i * 16 + 8) : 0;
	for (int i = 0; i < 4; i++) {
		if (!start[i]) continue;
		if (!v->rd(v->ctx, start[i], s) || (s[510] != 0x55) || (s[511] != 0xaa)) continue;
		if (((s[0] != 0xeb) && (s[0] != 0xe9)) || !fat_bpb(v, s)) continue;
		v->base = start[i];
		return 1;
	}
	return 0;
}

// the cluster after c, or 0 at the end of the chain
static unsigned int fat_next(fatVol* v, unsigned int c, unsigned char* s) {
	unsigned int off = c * (v->fat32 ? 4 : 2);
	if (!v->rd(v->ctx, v->base + v->fatStart + off / 512, s)) return 0;
	unsigned int n = v->fat32 ? (le32(s + off % 512) & 0x0fffffff) : le16(s + off % 512);
	if ((n < 2) || (n - 2 >= v->clusters)) return 0;
	return n;
}

// the n-th sector of a directory or file starting at clust; 0 past its end
static unsigned int fat_sector(fatVol* v, unsigned int clust, unsigned int n, unsigned char* s) {
	if (!clust) return (n < v->rootSecs) ? v->base + v->rootStart + n : 0;
	for (unsigned int k = n / v->spc; k; k--) {
		clust = fat_next(v, clust, s);
		if (!clust) return 0;
	}
	return v->base + v->dataStart + (clust - 2) * v->spc + n % v->spc;
}

static int name_eq(const char* a, int alen, const char* b) {
	int i;
	for (i = 0; (i < alen) && b[i]; i++) {
		if (toupper((unsigned char)a[i]) != toupper((unsigned char)b[i])) return 0;
	}
	return (i == alen) && !b[i];
}

// looks name (alen bytes) up in a directory
static int fat_find(fatVol* v, unsigned int dir, const char* name, int alen, fatEnt* e, unsigned char* s) {
	char lfn[261];
	int lfnok = 0;
	unsigned char lfnsum = 0;
	for (unsigned int n = 0; n < 0x10000; n++) {
		unsigned int lba = fat_sector(v, dir, n, s);
		if (!lba || !v->rd(v->ctx, lba, s)) return 0;
		for (int k = 0; k < 512; k += 32) {
			const unsigned char* d = s + k;
			if (!d[0]) return 0;
			if (d[0] == 0xe5) {
				lfnok = 0;
				continue;
			}
			if ((d[11] & 0x3f) == 0x0f) {
				// a long name part, ASCII only: the parts come last first
				int seq = (d[0] & 0x1f) - 1;
				static const int at[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
				if (d[0] & 0x40) {
					memset(lfn, 0, sizeof(lfn));
					lfnok = 1;
					lfnsum = d[13];
				}
				if (d[13] != lfnsum) lfnok = 0;
				if ((seq < 0) || (seq > 19)) lfnok = 0;
				for (int j = 0; lfnok && (j < 13); j++) {
					unsigned int ch = le16(d + at[j]);
					if ((ch == 0) || (ch == 0xffff)) break;
					lfn[seq * 13 + j] = (ch < 0x80) ? (char)ch : '?';
				}
				continue;
			}
			if (d[11] & 0x08) {		// the volume label
				lfnok = 0;
				continue;
			}
			unsigned char sum = 0;
			for (int j = 0; j < 11; j++) sum = (unsigned char)(((sum & 1) << 7) + (sum >> 1) + d[j]);
			if (sum != lfnsum) lfnok = 0;
			char sn[13];
			int p = 0;
			for (int j = 0; (j < 8) && (d[j] != ' '); j++) sn[p++] = d[j];
			if (d[8] != ' ') {
				sn[p++] = '.';
				for (int j = 8; (j < 11) && (d[j] != ' '); j++) sn[p++] = d[j];
			}
			sn[p] = 0;
			if (name_eq(name, alen, sn) || (lfnok && name_eq(name, alen, lfn))) {
				e->clust = le16(d + 26) | (v->fat32 ? (le16(d + 20) << 16) : 0);
				e->size = le32(d + 28);
				e->isdir = (d[11] & 0x10) ? 1 : 0;
				return 1;
			}
			lfnok = 0;
		}
	}
	return 0;
}

#define FAT_PRINT_DEPTH		32
#define FAT_PRINT_SECTORS	0x40000		// a bound on the sectors walked, for a broken volume

static unsigned long long fat_mix(unsigned long long h, const unsigned char* p, int n) {
	for (int i = 0; i < n; i++) {
		h ^= p[i];
		h *= 0x100000001b3ULL;
	}
	return h;
}

// An entry's share of the print: its name (the long one if it has one, without case),
// folder or not, and a file's size and time - not where it lies, not when it was made or
// read, not a folder's time: a folder copied or read again after the guest wrote to it
// lays out otherwise and is still the same card. The shares are added, so the order of a
// directory does not count either; the path counts through the parent's share.
static unsigned long long fat_print_entry(unsigned long long parent, const unsigned short* lfn, int lfnlen, const unsigned char* e) {
	unsigned short sn[12];
	unsigned char b[8];
	if (lfnlen <= 0) {			// the short name as it reads: "NAME.EXT"
		lfnlen = 0;
		for (int i = 0; (i < 8) && (e[i] != ' '); i++) sn[lfnlen++] = e[i];
		if (e[8] != ' ') {
			sn[lfnlen++] = '.';
			for (int i = 8; (i < 11) && (e[i] != ' '); i++) sn[lfnlen++] = e[i];
		}
		lfn = sn;
	}
	for (int i = 0; i < 8; i++) b[i] = (unsigned char)(parent >> (i * 8));
	unsigned long long h = fat_mix(0xcbf29ce484222325ULL, b, 8);
	for (int i = 0; i < lfnlen; i++) {
		unsigned int ch = lfn[i];
		if ((ch >= 'a') && (ch <= 'z')) ch -= 0x20;
		b[0] = ch & 0xff;
		b[1] = ch >> 8;
		h = fat_mix(h, b, 2);
	}
	b[0] = (e[11] & 0x10) ? 1 : 0;
	h = fat_mix(h, b, 1);
	if (!(e[11] & 0x10)) {
		h = fat_mix(h, e + 22, 4);		// written: time, date
		h = fat_mix(h, e + 28, 4);		// size
	}
	return h ? h : 1;
}

static void fat_print_dir(fatVol* v, unsigned int clust, int depth, unsigned long long self, unsigned long long* h, unsigned int* left, unsigned char* s) {
	unsigned char d[512];
	unsigned short lfn[20 * 13];
	int lfnlen = 0;
	unsigned char lfnsum = 0;
	for (unsigned int n = 0; *left; n++) {
		unsigned int lba = fat_sector(v, clust, n, s);
		if (!lba || !v->rd(v->ctx, lba, d)) return;
		(*left)--;
		for (int k = 0; k < 512; k += 32) {
			const unsigned char* e = d + k;
			if (!e[0]) return;
			if (e[0] == 0xe5) {
				lfnlen = 0;
				continue;
			}
			if ((e[11] & 0x3f) == 0x0f) {		// a long name part, the last one first
				static const int at[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
				int seq = (e[0] & 0x1f) - 1;
				if (e[0] & 0x40) {
					memset(lfn, 0, sizeof(lfn));	// a part that never comes adds nothing of another name
					lfnlen = 0;
					lfnsum = e[13];
					for (int j = 0; j < 13; j++) {
						unsigned int ch = le16(e + at[j]);
						if ((ch == 0) || (ch == 0xffff)) break;
						lfnlen = seq * 13 + j + 1;
					}
				}
				if ((seq < 0) || (seq > 19) || (e[13] != lfnsum)) {
					lfnlen = -1;		// broken: the short name stands
					continue;
				}
				for (int j = 0; (j < 13) && (lfnlen > 0); j++) {
					if (seq * 13 + j < lfnlen) lfn[seq * 13 + j] = (unsigned short)le16(e + at[j]);
				}
				continue;
			}
			int len = lfnlen;
			lfnlen = 0;
			if ((e[11] & 0x08) || (e[0] == '.')) continue;		// the label, dot and dotdot
			unsigned char sum = 0;
			for (int j = 0; j < 11; j++) sum = (unsigned char)(((sum & 1) << 7) + (sum >> 1) + e[j]);
			if (sum != lfnsum) len = 0;
			unsigned long long me = fat_print_entry(self, lfn, len, e);
			*h += me;
			if (!(e[11] & 0x10)) continue;
			unsigned int sub = le16(e + 26) | (v->fat32 ? (le16(e + 20) << 16) : 0);
			if ((sub >= 2) && (sub - 2 < v->clusters) && (depth < FAT_PRINT_DEPTH))
				fat_print_dir(v, sub, depth + 1, me, h, left, s);
		}
	}
}

unsigned long long fat_dir_print(fatSector rd, void* ctx) {
	unsigned char s[512];
	fatVol v;
	memset(&v, 0, sizeof(v));
	v.rd = rd;
	v.ctx = ctx;
	if (!fat_open(&v, s)) return 0;
	unsigned long long h = 0xcbf29ce484222325ULL;
	unsigned int left = FAT_PRINT_SECTORS;
	fat_print_dir(&v, v.rootClust, 0, 0, &h, &left, s);
	return h ? h : 1;
}

int fat_read_file(fatSector rd, void* ctx, const char* path, unsigned char* buf, int max) {
	unsigned char s[512];
	fatVol v;
	memset(&v, 0, sizeof(v));
	v.rd = rd;
	v.ctx = ctx;
	if (!fat_open(&v, s)) return -1;
	fatEnt e = {v.rootClust, 0, 1};
	while (*path) {
		while (*path == '/') path++;
		const char* end = strchr(path, '/');
		int len = end ? (int)(end - path) : (int)strlen(path);
		if (!len) break;
		if (!e.isdir || !fat_find(&v, e.clust, path, len, &e, s)) return -1;
		path += len;
	}
	if (e.isdir) return -1;
	int got = 0;
	unsigned int clust = e.clust;
	while ((got < max) && ((unsigned int)got < e.size) && (clust >= 2)) {
		for (unsigned int k = 0; (k < v.spc) && (got < max) && ((unsigned int)got < e.size); k++) {
			if (!rd(ctx, v.base + v.dataStart + (clust - 2) * v.spc + k, s)) return -1;
			int n = (int)e.size - got;
			if (n > 512) n = 512;
			if (n > max - got) n = max - got;
			memcpy(buf + got, s, n);
			got += n;
		}
		clust = fat_next(&v, clust, s);
	}
	return ((unsigned int)got < e.size) && (got < max) ? -1 : (int)e.size;
}
