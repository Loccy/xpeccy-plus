#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Reads one file off a FAT16 or FAT32 volume, a sector at a time, the way a
// machine's firmware finds its files on a card: an MBR with a FAT partition or a
// volume from sector 0. Read only.

// one 512-byte sector: 1 if it was read
typedef int (*fatSector)(void* ctx, unsigned int lba, unsigned char* buf);

// path: "dir/dir/name.ext", each part matched without case against the short or
// the long name; up to max bytes go into buf. The file's length, or -1 if it is
// not there or the volume is not one this reads.
int fat_read_file(fatSector rd, void* ctx, const char* path, unsigned char* buf, int max);

// A print of the volume's directory tree as a reader sees it: every entry of every
// directory, long names too, the last access date left out (a read may move it). Files
// added, gone, grown or written change it. 0 when it is no volume this reads.
unsigned long long fat_dir_print(fatSector rd, void* ctx);

#ifdef __cplusplus
}
#endif
