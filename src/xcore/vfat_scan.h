#pragma once

#include <QString>

#include "../libxpeccy/vfat.h"
#include "../libxpeccy/spectrum.h"	// sdcard.h has no extern "C" of its own

// build a volume from a host folder (and another as a folder of its root), NULL if it can't be read
vFat* vfat_scan(const QString&, bool writable = false, const QString& sub = QString(), const QString& subName = QString());
vFat* vfat_scan_sized(const QString&, unsigned int sectors);	// a volume of that many sectors, for an image
void sdc_mount(SDCard*, const QString&);	// mount an image file or a host folder, whichever the path is
void sdc_remount(SDCard*);		// re-read what is mounted (a folder may have changed)
bool sdc_mount_over(SDCard*, const QString& sub, const QString& subName);	// a folder card read again with another folder in its root

void ide_mount(IDE*, int, const QString&);	// same for one IDE device (IDE_MASTER / IDE_SLAVE)
void ide_remount(IDE*);

vFat* fold_waiting(Computer*);			// a volume on hold the guest has written to, NULL if none
void fold_release(Computer*, bool allow);	// the user's word on every volume on hold
void fold_read_only(Computer*);		// Folders set to R/O: every volume in use but the Next's own card
QString fold_news(Computer*);			// what a volume has to tell the user, once; empty when nothing new
