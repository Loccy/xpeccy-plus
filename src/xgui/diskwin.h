#pragma once

#include <functional>
#include <QDialog>
#include <QTabBar>
#include <QToolButton>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QTimer>
#include <QVector>

#include "options/opt_diskcat.h"

// Disk manager: what is in a drive, what is on the disk, and taking files off
// it; a TR-DOS or +3DOS disk has a catalog to show. While it is up it watches
// the drives, so a file written by the machine or copied in from the tape shows.

enum {
	DW_OPEN = 0,
	DW_NEW,
	DW_SAVE,
	DW_SAVE_AS,
	DW_EJECT
};

enum {
	DK_NONE = 0,
	DK_TRD,
	DK_P3
};

class xDiskWin : public QDialog {
	public:
		xDiskWin(QWidget* = NULL);
		void showWindow(int drv = -1);	// on that drive's tab
		void refresh();
		std::function<void()> tapeChanged;	// files went onto the tape
		std::function<void(int, int)> diskOp;	// DW_* on a drive, done by the main window
	private:
		QTabBar* tabs;
		QCheckBox* fast;
		QLineEdit* path;
		QToolButton* btnOpen;
		QToolButton* btnNew;
		QToolButton* btnSave;
		QToolButton* btnSaveAs;
		QToolButton* btnEject;
		QCheckBox* protect;
		xDiskCatTable* list;
		QLabel* note;
		QToolButton* toTape;
		QToolButton* toHobeta;
		QToolButton* toRaw;
		QLabel* head;
		QTimer* watch;
		int kind = DK_NONE;		// the file system of the disk shown
		QList<int> rows;		// catalog index of each row: deleted files are not shown
		QList<QList<int>> rowTrks;	// the tracks each row's file is on, for finding the one under the head
		int sides = 2;			// of a TR-DOS disk, as its system sector says
		p3Disk p3dsk;			// its layout, on a +3DOS disk
		QVector<p3File> p3files;
		QList<QByteArray> seen;		// each drive as the window last showed it
		int headTrack(Floppy*, int);
		void fillTRD(Floppy*, const unsigned char*, QStringList&);
		void fillP3(Floppy*, QStringList&);
		void p3ToTape(QList<int>);
		void p3Save(QList<int>, QString, bool);
		int drive();
		QList<int> picked();
		QByteArray driveState(int);
		QString driveName(int);
		void watchDrives();
		void showHead();
		void fill();
		void fillDrive(Floppy*);
		void doOp(int);
		void setColumns();
		void pickedChanged();
		void copyToTape();
		void saveFiles(bool hobeta);
};
