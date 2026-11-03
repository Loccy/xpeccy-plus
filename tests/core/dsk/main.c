// Headless +3 disk bench: EXTENDED DSK images read into a drive, and the uPD765
// asked for them the way a program on the +3 asks, through its two ports. A
// section each:
//
//	read/<case>	a command, its result bytes, and which sector each block of
//			data the CPU took came from
//	roundtrip	the image saved back, compared with the file it was read from
//
//	dsk-bench			the cases, on an image the bench writes
//	dsk-bench -sweep file.dsk...	load + save for each file, one line a file

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spectrum.h"
#include "filetypes/filetypes.h"

// ---- the image

typedef struct {
	unsigned char c, h, r, n, st1, st2;
	int len;		// bytes of one read stored
	int copies;
} tSec;

typedef struct {
	int nsec;
	unsigned char n, gap;
	tSec s[29];
} tTrk;

static unsigned char pattern(int t, int r, int k, int copy) {
	return (t * 59 + r * 31 + k * 7 + copy * 101 + (k >> 8) * 13) & 0xff;
}

static int track_bytes(tTrk* t) {
	int sz = 0x100;
	for (int i = 0; i < t->nsec; i++)
		sz += t->s[i].len * t->s[i].copies;
	return t->nsec ? ((sz + 0xff) & ~0xff) : 0;
}

static int write_edsk(const char* path, tTrk* trk, int ntrk) {
	FILE* f = fopen(path, "wb");
	if (!f) return 0;
	unsigned char hdr[0x100];
	memset(hdr, 0, sizeof(hdr));
	memcpy(hdr, "EXTENDED CPC DSK File\r\nDisk-Info\r\n", 34);
	memcpy(hdr + 0x22, "dsk-bench     ", 14);
	hdr[0x30] = ntrk;
	hdr[0x31] = 1;
	for (int i = 0; i < ntrk; i++)
		hdr[0x34 + i] = track_bytes(&trk[i]) >> 8;
	fwrite(hdr, 1, 0x100, f);
	for (int i = 0; i < ntrk; i++) {
		tTrk* t = &trk[i];
		if (!t->nsec) continue;
		unsigned char tib[0x100];
		memset(tib, 0, sizeof(tib));
		memcpy(tib, "Track-Info\r\n", 12);
		tib[0x10] = i;
		tib[0x14] = t->n;
		tib[0x15] = t->nsec;
		tib[0x16] = t->gap;
		tib[0x17] = 0xe5;
		for (int k = 0; k < t->nsec; k++) {
			tSec* s = &t->s[k];
			unsigned char* p = tib + 0x18 + 8 * k;
			int len = s->len * s->copies;
			p[0] = s->c; p[1] = s->h; p[2] = s->r; p[3] = s->n;
			p[4] = s->st1; p[5] = s->st2;
			p[6] = len & 0xff; p[7] = (len >> 8) & 0xff;
		}
		fwrite(tib, 1, 0x100, f);
		int sz = 0x100;
		for (int k = 0; k < t->nsec; k++) {
			tSec* s = &t->s[k];
			for (int c = 0; c < s->copies; c++)
				for (int b = 0; b < s->len; b++)
					fputc(pattern(i, s->r, b, c), f);
			sz += s->len * s->copies;
		}
		for (; sz < track_bytes(t); sz++)
			fputc(0, f);
	}
	fclose(f);
	return 1;
}

static tTrk trk[5];

static void add(int t, int c, int h, int r, int n, int st1, int st2, int len, int copies) {
	tSec* s = &trk[t].s[trk[t].nsec++];
	s->c = c; s->h = h; s->r = r; s->n = n;
	s->st1 = st1; s->st2 = st2;
	s->len = len;
	s->copies = copies;
}

static void make_image(void) {
	memset(trk, 0, sizeof(trk));
	for (int t = 0; t < 5; t++) {
		trk[t].n = 2;
		trk[t].gap = 0x4e;
	}
	for (int r = 1; r <= 9; r++)
		add(0, 0, 0, r, 2, 0, 0, 512, 1);
	add(1, 1, 0, 1, 2, 0, 0, 512, 1);
	add(1, 1, 0, 2, 2, 0, 0x40, 512, 1);		// deleted data mark
	add(1, 1, 0, 3, 2, 0x20, 0x20, 512, 1);		// CRC error in the data
	add(1, 0x11, 0, 4, 2, 0, 0, 512, 1);		// C is not the track's
	add(1, 0xff, 0, 5, 2, 0, 0, 512, 1);		// C = FF
	add(1, 1, 0, 6, 2, 0x20, 0, 512, 1);		// CRC error in the ID
	add(1, 1, 0, 7, 2, 0x01, 0x01, 0, 1);		// no data mark
	add(1, 1, 0, 8, 1, 0, 0, 256, 1);		// N 1 on a track of N 2
	add(1, 1, 0, 9, 2, 0x20, 0x20, 512, 3);		// weak: three reads
	trk[2].n = 6;
	add(2, 2, 0, 1, 6, 0x20, 0x20, 6144, 1);	// N 6: runs to the index
	// track 3 unformatted
	trk[4].n = 0;
	for (int r = 1; r <= 4; r++)
		add(4, 4, 0, r, 0, 0, 0, 128, 1);
}

// ---- the +3's ports

static DiskIF* dif;

static void dummy_irq(int id, void* p) {(void)id; (void)p;}

// in steps of an odd length, as the CPU's are: a door's delay run down to
// exactly 0 would never close it (flp_sync waits for below 0)
static void run(int ns) {
	while (ns > 0) {
		int n = (ns > 3999) ? 3999 : ns;
		difSync(dif, n);
		ns -= n;
	}
}

static int msr(void) {
	int r = 0xff;
	difIn(dif, 0x2ffd, &r, 0);
	return r;
}

static int rd(void) {
	int r = 0xff;
	difIn(dif, 0x3ffd, &r, 0);
	return r;
}

static int send(const unsigned char* b, int n) {
	for (int i = 0; i < n; i++) {
		int t = 0;
		while ((msr() & 0xc0) != 0x80) {
			run(2000);
			if (++t > 500000) return 0;
		}
		difOut(dif, 0x3ffd, b[i], 0);
		run(2000);
	}
	return 1;
}

// the execution phase, the CPU taking a byte every cpu_ns until it has taken max
static int take(unsigned char* buf, int max, int cpu_ns) {
	int got = 0;
	for (long t = 0; t < 4000000000L; t += cpu_ns) {
		int m = msr();
		if (!(m & 0x20)) break;			// EXM off: the result phase
		if (((m & 0xc0) == 0xc0) && (got < max)) buf[got++] = rd();
		run(cpu_ns);
	}
	return got;
}

static int result(unsigned char* res) {
	int n = 0;
	for (int t = 0; (t < 1000000) && (n < 16); t++) {
		int m = msr();
		if ((m & 0xd0) == 0xd0) {
			res[n++] = rd();
		} else if (!(m & 0x10)) {
			break;
		}
		run(2000);
	}
	return n;
}

static void seek(int cyl) {
	unsigned char c[] = {0x0f, 0x00, cyl};
	unsigned char s[] = {0x08};
	unsigned char res[16];
	send(c, 3);
	run(200000000);
	send(s, 1);
	result(res);
}

// which sectors of the image the bytes taken are, a block of 128 << n each
static void describe(unsigned char* buf, int got, int n) {
	int blk = (n > 7) ? 0x4000 : (0x80 << n);
	printf("  data %d:", got);
	for (int off = 0; off < got; off += blk) {
		int len = (got - off < blk) ? got - off : blk;
		int best = 0;
		char name[32] = "?";
		for (int t = 0; t < 5; t++) {
			for (int k = 0; k < trk[t].nsec; k++) {
				tSec* s = &trk[t].s[k];
				for (int c = 0; c < s->copies; c++) {
					int m = 0;
					while ((m < len) && (m < s->len) && (buf[off + m] == pattern(t, s->r, m, c))) m++;
					if (m > best) {
						best = m;
						if (s->copies > 1) {
							sprintf(name, "T%dR%d/%d", t, s->r, c);
						} else {
							sprintf(name, "T%dR%d", t, s->r);
						}
					}
				}
			}
		}
		if (best == len) {
			printf(" %s", name);
		} else if (best > 0) {
			printf(" %s(%d)", name, best);
		} else {
			printf(" ?");
		}
	}
	printf("\n");
}

static unsigned char data[0x10000];

// one read command: its bytes, the CPU taking at most max bytes
static void cmd(const char* name, int op, int cyl, int c, int h, int r, int n, int eot, int dtl, int max) {
	unsigned char b[9] = {op, 0x00, c, h, r, n, eot, 0x2a, dtl};
	unsigned char res[16];
	seek(cyl);
	printf("== read/%s\n", name);
	printf("  cmd %.2X: C%.2X H%.2X R%.2X N%.2X EOT%.2X DTL%.2X\n", op, c, h, r, n, eot, dtl);
	if (!send(b, 9)) {
		printf("  command not taken\n");
		return;
	}
	int got = take(data, max, 10000);
	int nr = result(res);
	printf("  result");
	for (int i = 0; i < nr; i++) printf(" %.2X", res[i]);
	printf("\n");
	if (got) describe(data, got, n);
}

static void read_id(const char* name, int cyl) {
	unsigned char b[2] = {0x4a, 0x00};
	unsigned char res[16];
	seek(cyl);
	printf("== read/%s\n", name);
	send(b, 2);
	int nr = result(res);
	printf("  result");
	for (int i = 0; i < nr && i < 3; i++) printf(" %.2X", res[i]);
	if ((nr == 7) && !(res[0] & 0xc0)) printf(" - %.2X %.2X %.2X %.2X", res[3], res[4], res[5], res[6]);
	printf("\n");
}

// three reads of the weak sector: a different read each time, all CRC errors
static void weak(void) {
	unsigned char b[9] = {0x46, 0x00, 1, 0, 9, 2, 9, 0x2a, 0xff};
	unsigned char res[16];
	int copy[3];
	seek(1);
	printf("== read/weak\n");
	for (int i = 0; i < 3; i++) {
		send(b, 9);
		int got = take(data, 512, 10000);
		result(res);
		copy[i] = -1;
		for (int c = 0; c < 3; c++) {
			int m = 0;
			while ((m < got) && (data[m] == pattern(1, 9, m, c))) m++;
			if (m == 512) copy[i] = c;
		}
		printf("  result %.2X %.2X %.2X, read %d\n", res[0], res[1], res[2], copy[i]);
	}
	printf("  all three differ: %s\n", ((copy[0] != copy[1]) && (copy[1] != copy[2]) && (copy[0] != copy[2])) ? "yes" : "no");
}

// ---- saving back

static unsigned char* read_all(const char* path, long* len) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	*len = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char* buf = malloc(*len > 0 ? *len : 1);
	if (fread(buf, 1, *len, f) != (size_t)*len) *len = 0;
	fclose(f);
	return buf;
}

// The image saved against the file, past the creator's name and the track count
// (42 or 84 when saved): "same", or where it first differs and in which track.
// derived: what is on the tracks alone, the blocks kept from the file dropped.
static void compare(Floppy* flp, const unsigned char* file, long flen, int derived, char* out, size_t osz) {
	if (derived) {
		for (int i = 0; i < 256; i++) {
			free(flp->data[i].raw);
			flp->data[i].raw = NULL;
			flp->data[i].rawlen = 0;
		}
	}
	size_t len = 0;
	unsigned char* img = dsk_build(flp, &len);
	if (!img) {
		snprintf(out, osz, "not built");
		return;
	}
	long n = ((long)len < flen) ? (long)len : flen;
	long at = -1;
	for (long i = 0x31; i < n; i++) {
		if (img[i] != file[i]) {
			at = i;
			break;
		}
	}
	if ((at < 0) && ((long)len != flen)) at = n;
	if (at < 0) {
		snprintf(out, osz, "same");
	} else {
		int trk = -1;
		long pos = 0x100;
		for (int t = 0; (t < 0xcc) && (pos <= at); t++) {
			long sz = file[0x34 + t] << 8;
			if ((at >= pos) && (at < pos + sz)) trk = t;
			pos += sz;
		}
		snprintf(out, osz, "differs at %lX (track %d), %ld bytes against %ld", at, trk, (long)len, flen);
	}
	free(img);
}

static void roundtrip(const char* path, const char* name) {
	long flen = 0;
	unsigned char* file = read_all(path, &flen);
	char res[2][128];
	for (int k = 0; k < 2; k++) {
		Floppy* flp = flpCreate(0, dummy_irq, NULL);
		dsk_read(flp, path);
		compare(flp, file, flen, k, res[k], sizeof(res[k]));
		flpDestroy(flp);
	}
	if (name) {
		printf("== roundtrip\n  kept: %s\n  derived: %s\n", res[0], res[1]);
	} else {
		printf("%s\tkept: %s\tderived: %s\n", path, res[0], res[1]);
	}
	free(file);
}

static void start(const char* path) {
	fdcFlag = 0;
	dif = difCreate(DIF_P3DOS, dummy_irq, NULL);
	difReset(dif);
	for (int i = 0; i < 4; i++) dif->flp[i]->motor = 1;
	int err = dsk_read(dif->flp[0], path);
	if (err) printf("dsk_read: %d\n", err);
	run(600000000);			// the door closes
	unsigned char spec[] = {0x03, 0xaf, 0x03};	// as +3DOS: non-DMA
	unsigned char cal[] = {0x07, 0x00};
	send(spec, 3);
	send(cal, 2);
	run(200000000);
	seek(0);
}

static int cases(const char* tmp) {
	make_image();
	if (!write_edsk(tmp, trk, 5)) {
		printf("can't write %s\n", tmp);
		return 1;
	}
	start(tmp);
	read_id("id-t0", 0);
	cmd("eot", 0x46, 0, 0, 0, 1, 2, 1, 0xff, 0x10000);
	cmd("multi", 0x46, 0, 0, 0, 1, 2, 3, 0xff, 0x10000);
	cmd("past-eot", 0x46, 0, 0, 0, 9, 2, 5, 0xff, 0x10000);
	cmd("overrun", 0x46, 0, 0, 0, 1, 2, 9, 0xff, 100);
	cmd("deleted", 0x46, 1, 1, 0, 2, 2, 9, 0xff, 0x10000);
	cmd("deleted-sk", 0x66, 1, 1, 0, 2, 2, 2, 0xff, 0x10000);
	cmd("deleted-sk-next", 0x66, 1, 1, 0, 1, 2, 3, 0xff, 0x10000);
	cmd("read-deleted-normal", 0x4c, 1, 1, 0, 1, 2, 1, 0xff, 0x10000);
	cmd("read-deleted", 0x4c, 1, 1, 0, 2, 2, 2, 0xff, 0x10000);
	cmd("data-crc", 0x46, 1, 1, 0, 3, 2, 9, 0xff, 0x10000);
	cmd("wrong-cyl", 0x46, 1, 1, 0, 4, 2, 4, 0xff, 0x10000);
	cmd("its-cyl", 0x46, 1, 0x11, 0, 4, 2, 4, 0xff, 0x10000);
	cmd("bad-cyl", 0x46, 1, 1, 0, 5, 2, 5, 0xff, 0x10000);
	cmd("id-crc", 0x46, 1, 1, 0, 6, 2, 6, 0xff, 0x10000);
	cmd("no-data-mark", 0x46, 1, 1, 0, 7, 2, 7, 0xff, 0x10000);
	cmd("n-differs", 0x46, 1, 1, 0, 8, 2, 8, 0xff, 0x10000);
	cmd("n-1", 0x46, 1, 1, 0, 8, 1, 8, 0xff, 0x10000);
	weak();
	cmd("long", 0x46, 2, 2, 0, 1, 6, 1, 0xff, 0x10000);
	read_id("id-unformatted", 3);
	cmd("unformatted", 0x46, 3, 3, 0, 1, 2, 1, 0xff, 0x10000);
	cmd("dtl", 0x46, 4, 4, 0, 1, 0, 1, 0x40, 0x10000);
	cmd("track-t0", 0x42, 0, 0, 0, 1, 2, 9, 0xff, 0x10000);
	cmd("track-t1", 0x42, 1, 1, 0, 1, 2, 9, 0xff, 0x10000);
	fdcFlag = FDC_FAST;
	cmd("turbo-multi", 0x46, 0, 0, 0, 1, 2, 3, 0xff, 0x10000);
	cmd("turbo-overrun", 0x46, 0, 0, 0, 1, 2, 9, 0xff, 100);
	fdcFlag = 0;
	difDestroy(dif);
	roundtrip(tmp, "bench");
	remove(tmp);
	return 0;
}

int main(int ac, char** av) {
	const char* tdir = getenv("TEMP");
	if (!tdir) tdir = getenv("TMPDIR");
	if (!tdir) tdir = "/tmp";
	char tmp[1024];
	snprintf(tmp, sizeof(tmp), "%s/dsk-bench-%d.dsk", tdir, (int)(sizeof(void*)));
	if ((ac > 1) && !strcmp(av[1], "-sweep")) {
		for (int i = 2; i < ac; i++)
			roundtrip(av[i], NULL);
		return 0;
	}
	return cases(tmp);
}
