#include <string.h>
#include "filetypes.h"

// The spec +3DOS takes when the boot sector holds none, one per format that
// can lack one
static const unsigned char p3_spec_def[3][8] = {
	{0x00, 0x00, 40, 9, 2, 1, 3, 2},	// +3: single sided, 1K blocks, 64 entries
	{0x01, 0x00, 40, 9, 2, 2, 3, 2},	// CPC system
	{0x02, 0x00, 40, 9, 2, 0, 3, 2}		// CPC data
};

// a logical track as the drive holds it, cylinder * 2 + head
static int p3_phys(p3Disk* dsk, int trk) {
	int cyl = trk;
	int hd = 0;
	if (dsk->sides == P3_SIDES_ALT) {
		cyl = trk >> 1;
		hd = trk & 1;
	} else if ((dsk->sides == P3_SIDES_SUCC) && (trk >= dsk->tracks)) {
		cyl = trk - dsk->tracks;
		hd = 1;
	}
	return (cyl << 1) | hd;
}

// and back: where a head stands, as the format numbers its tracks
int p3_logical(p3Disk* dsk, int cyl, int hd) {
	if (dsk->sides == P3_SIDES_ALT) return (cyl << 1) | hd;
	if (dsk->sides == P3_SIDES_SUCC) return hd ? dsk->tracks + cyl : cyl;
	return cyl;
}

// the 128-byte record of the data area, block 0 being the directory's first
static unsigned char* p3_record(Floppy* flp, p3Disk* dsk, int rec) {
	int per = dsk->seclen / 128;
	int sec = dsk->reserved * dsk->spt + rec / per;
	int idx = p3_phys(dsk, sec / dsk->spt);
	if (idx > 255) return NULL;
	unsigned char* ptr = diskGetSectorDataPtr(flp, idx, (dsk->first + sec % dsk->spt) & 0xff);
	return ptr ? ptr + (rec % per) * 128 : NULL;
}

static int p3_set(p3Disk* dsk, const unsigned char* spec, int first) {
	int nsides = (spec[1] & 3) ? 2 : 1;
	if ((spec[1] & 3) == 3) return 0;
	if ((spec[2] < 1) || (spec[2] > 86) || (spec[3] < 1) || (spec[3] > 18)) return 0;
	// the allocation bits of CP/M 2.2 can reserve no more than 16 directory blocks
	if ((spec[4] > 3) || (spec[6] < 3) || (spec[6] > 7) || (spec[7] < 1) || (spec[7] > 16)) return 0;
	if (spec[5] >= spec[2] * nsides) return 0;
	dsk->format = spec[0];
	dsk->sides = spec[1] & 3;
	dsk->tracks = spec[2];
	dsk->spt = spec[3];
	dsk->first = first;
	dsk->seclen = 128 << spec[4];
	dsk->reserved = spec[5];
	dsk->blklen = 128 << spec[6];
	dsk->dirblk = spec[7];
	dsk->blocks = (dsk->tracks * nsides - dsk->reserved) * dsk->spt * dsk->seclen / dsk->blklen;
	return (dsk->dirblk < dsk->blocks);
}

// Every entry is either free, a file of user 0..15, a label or time stamps,
// and names are text: what else has sector 1 at 512 bytes is not taken for one
static int p3_dir_ok(Floppy* flp, p3Disk* dsk) {
	int recs = dsk->dirblk * dsk->blklen / 128;
	for (int r = 0; r < recs; r++) {
		unsigned char* ptr = p3_record(flp, dsk, r);
		if (!ptr) return 0;
		for (int e = 0; e < 4; e++, ptr += 32) {
			if (ptr[0] == 0xe5) continue;
			if (ptr[0] > 0x21) return 0;
			if (ptr[0] > 15) continue;
			for (int i = 1; i < 12; i++) {
				if ((ptr[i] & 0x7f) < 0x20) return 0;
			}
		}
	}
	return 1;
}

int p3_disk(Floppy* flp, p3Disk* dsk) {
	if (!flp->insert) return 0;
	unsigned char* bs = diskGetSectorDataPtr(flp, 0, 1);
	int ok = 0;
	if (bs) {
		// only formats 0 and 3 carry a spec; anything else in the byte is boot code
		if ((bs[0] == 0) || (bs[0] == 3)) ok = p3_set(dsk, bs, 1);
		if (!ok) ok = p3_set(dsk, p3_spec_def[P3_PLUS3], 1);
	} else if (diskGetSectorDataPtr(flp, 0, 0x41)) {
		ok = p3_set(dsk, p3_spec_def[P3_CPC_SYS], 0x41);
	} else if (diskGetSectorDataPtr(flp, 0, 0xc1)) {
		ok = p3_set(dsk, p3_spec_def[P3_CPC_DATA], 0xc1);
	}
	return ok && p3_dir_ok(flp, dsk);
}

int p3_boots(Floppy* flp) {
	if (!flp->insert) return 0;
	unsigned char* bs = diskGetSectorDataPtr(flp, 0, 1);
	if (!bs) return 0;
	int sum = 0;
	for (int i = 0; i < 512; i++) sum += bs[i];
	return (sum & 0xff) == 3;
}

// the directory as it is on the disk, for noticing a change
int p3_dir_raw(Floppy* flp, p3Disk* dsk, unsigned char* buf, int max) {
	int recs = dsk->dirblk * dsk->blklen / 128;
	int len = 0;
	for (int r = 0; (r < recs) && (len + 128 <= max); r++) {
		unsigned char* ptr = p3_record(flp, dsk, r);
		if (!ptr) break;
		memcpy(buf + len, ptr, 128);
		len += 128;
	}
	return len;
}

static void p3_header(Floppy* flp, p3Disk* dsk, p3File* f) {
	f->hdr = 0;
	if ((f->nblk < 1) || (f->blk[0] == 0) || (f->records < 1)) return;
	unsigned char* h = p3_record(flp, dsk, f->blk[0] * (dsk->blklen / 128));
	if (!h || memcmp(h, "PLUS3DOS\x1a", 9)) return;
	unsigned char sum = 0;
	for (int i = 0; i < 127; i++) sum += h[i];
	if (sum != h[127]) return;
	f->hdr = 1;
	f->type = h[15];
	f->len = h[16] | (h[17] << 8);
	f->param1 = h[18] | (h[19] << 8);
	f->param2 = h[20] | (h[21] << 8);
}

// The files of all users, each extent put in its place by its number. Returns
// how many were found, max at most.
int p3_catalog(Floppy* flp, p3Disk* dsk, p3File* out, int max) {
	int big = (dsk->blocks > 255);
	int ptrs = big ? 8 : 16;				// block numbers in an entry
	int exm = dsk->blklen / (big ? 2048 : 1024) - 1;	// 16K extents in an entry, less one
	int recs = dsk->dirblk * dsk->blklen / 128;
	int cnt = 0;
	char name[9];
	char ext[4];
	for (int r = 0; r < recs; r++) {
		unsigned char* ptr = p3_record(flp, dsk, r);
		if (!ptr) break;
		for (int e = 0; e < 4; e++, ptr += 32) {
			if (ptr[0] > 15) continue;
			for (int i = 0; i < 8; i++) name[i] = ptr[1 + i] & 0x7f;
			for (int i = 0; i < 3; i++) ext[i] = ptr[9 + i] & 0x7f;
			name[8] = 0;
			ext[3] = 0;
			p3File* f = NULL;
			for (int i = 0; (i < cnt) && !f; i++) {
				if ((out[i].user == ptr[0]) && !strcmp(out[i].name, name) && !strcmp(out[i].ext, ext))
					f = &out[i];
			}
			if (!f) {
				if (cnt >= max) continue;
				f = &out[cnt++];
				memset(f, 0, sizeof(p3File));
				f->user = ptr[0];
				strcpy(f->name, name);
				strcpy(f->ext, ext);
			}
			int xn = (ptr[12] & 0x1f) | ((ptr[14] & 0x3f) << 5);
			int total = xn * 128 + (ptr[15] & 0xff);
			if (total > f->records) f->records = total;
			int pos = (xn / (exm + 1)) * ptrs;
			for (int k = 0; k < ptrs; k++) {
				int b = big ? (ptr[16 + k * 2] | (ptr[17 + k * 2] << 8)) : ptr[16 + k];
				if ((b == 0) || (b >= dsk->blocks) || (pos + k >= P3_MAXBLK)) continue;
				if (!f->blk[pos + k]) f->used++;
				f->blk[pos + k] = b;
				if (pos + k >= f->nblk) f->nblk = pos + k + 1;
			}
		}
	}
	// what the entries say is the size may run past the blocks they hold
	int rpb = dsk->blklen / 128;
	for (int i = 0; i < cnt; i++) {
		if (out[i].records > out[i].nblk * rpb) out[i].records = out[i].nblk * rpb;
		p3_header(flp, dsk, &out[i]);
	}
	return cnt;
}

// The whole file, header included, up to max bytes; a hole reads as zeros.
// -1 when a sector is missing.
int p3_read(Floppy* flp, p3Disk* dsk, p3File* f, unsigned char* buf, int max) {
	int rpb = dsk->blklen / 128;
	int len = 0;
	for (int r = 0; (r < f->records) && (len + 128 <= max); r++) {
		int b = f->blk[r / rpb];
		if (b == 0) {
			memset(buf + len, 0, 128);
		} else {
			unsigned char* ptr = p3_record(flp, dsk, b * rpb + r % rpb);
			if (!ptr) return -1;
			memcpy(buf + len, ptr, 128);
		}
		len += 128;
	}
	return len;
}

int p3_free_blocks(p3Disk* dsk, p3File* files, int cnt) {
	int used = dsk->dirblk;
	for (int i = 0; i < cnt; i++) used += files[i].used;
	return (used < dsk->blocks) ? dsk->blocks - used : 0;
}

// a name the host takes: what Windows refuses replaced, trailing spaces cut
static void p3_host_name(const char* src, char* dst) {
	int n = 0;
	for (; *src; src++) dst[n++] = strchr("\\/:*?\"<>|", *src) ? '_' : *src;
	while ((n > 0) && (dst[n - 1] == ' ')) n--;
	dst[n] = 0;
}

// The data alone, or the whole file when it has no header: name.ext in dir
int p3_save_raw(Floppy* flp, p3Disk* dsk, p3File* f, const char* dir) {
	int size = f->records * 128;
	unsigned char* buf = malloc(size + 1);
	int got = p3_read(flp, dsk, f, buf, size);
	int err = ERR_OK;
	if (got < 0) {
		err = ERR_TRD_SNF;
	} else {
		char name[9];
		char ext[4];
		p3_host_name(f->name, name);
		p3_host_name(f->ext, ext);
		char path[strlen(dir) + 16];
		sprintf(path, "%s%s%s%s%s", dir, SLASH, name, *ext ? "." : "", ext);
		FILE* file = fopen(path, "wb");
		if (!file) {
			err = ERR_CANT_OPEN;
		} else {
			if (f->hdr && (got >= f->len + 128)) {
				fwrite(buf + 128, f->len, 1, file);
			} else {
				fwrite(buf, got, 1, file);
			}
			fclose(file);
		}
	}
	free(buf);
	return err;
}

// Hobeta is a TR-DOS file, so the +3DOS header says what it is to be: a
// program keeps its autostart line after the data, as TR-DOS does, and its
// start is the length with the variables, its length the program alone.
int p3_save_hobeta(Floppy* flp, p3Disk* dsk, p3File* f, const char* dir) {
	if (!f->hdr || (f->type > 3)) return ERR_TRD_SNF;
	int size = f->records * 128 + 4;
	unsigned char* buf = calloc(size + 256, 1);
	int got = p3_read(flp, dsk, f, buf, f->records * 128);
	int err = ERR_OK;
	TRFile dsc;
	unsigned char* data = buf + 128;
	int start = f->param1;
	int len = f->len;
	int tot = f->len;
	if (got < f->len + 128) {
		err = ERR_TRD_SNF;
	} else if (f->type == 0) {
		dsc.ext = 'B';
		start = f->len;
		len = f->param2;
		data[tot++] = 0x80;
		data[tot++] = 0xaa;
		data[tot++] = f->param1 & 0xff;
		data[tot++] = (f->param1 >> 8) & 0xff;
	} else {
		dsc.ext = (f->type == 3) ? 'C' : 'D';
	}
	int slen = (tot + 255) / 256;
	if (slen > 255) err = ERR_TRD_SNF;		// past what a TR-DOS file can hold
	if (err == ERR_OK) {
		memset(data + tot, 0, slen * 256 - tot);
		dsc = diskMakeDescriptor(f->name, dsc.ext, start, len);
		dsc.slen = slen;		// the autostart line after a program counts too
		dsc.sec = 0;
		dsc.trk = 0;
		char name[9];
		p3_host_name(f->name, name);
		char path[strlen(dir) + 16];
		sprintf(path, "%s%s%s.$%c", dir, SLASH, name, dsc.ext);
		err = saveHobeta(dsc, (char*)data, path);
	}
	free(buf);
	return err;
}

// the logical track a block starts on, both sides counted as the format counts
// them, the one it ends on, and the id of its first sector
int p3_block_track(p3Disk* dsk, int blk, int* last, int* sec) {
	int s = dsk->reserved * dsk->spt + blk * dsk->blklen / dsk->seclen;
	if (last) *last = (s + dsk->blklen / dsk->seclen - 1) / dsk->spt;
	if (sec) *sec = dsk->first + s % dsk->spt;
	return s / dsk->spt;
}
