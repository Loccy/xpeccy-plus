#include "dbg_memfill.h"
#include "../../xcore/xcore.h"
#include "../../libxpeccy/filetypes/filetypes.h"

#include <QDebug>

xMemFiller::xMemFiller(QWidget* p):QDialog(p) {
	ui.setupUi(this);
	ui.leStartHex->setXFlag(XHS_DEC | XHS_ADDR);
	ui.leEndHex->setXFlag(XHS_DEC | XHS_ADDR);
	connect(ui.cbMethod, SIGNAL(currentIndexChanged(int)),this,SLOT(metChange()));
	connect(ui.pbFill,SIGNAL(clicked(bool)),this,SLOT(fill()));
}

void xMemFiller::start(Computer* c, int bgn, int end) {
	comp = c;
	if ((bgn > -1) && (end > -1)) {
		ui.leStartHex->setValue(bgn);
		ui.leEndHex->setValue(end);
	}
	show();
}

void xMemFiller::metChange() {
	ui.leMask->setEnabled(ui.cbMethod->currentText() == "Mask");
}

int xMemFiller::mrd(int adr) {
	return comp_peek(comp, adr, MEM_VIEW_READ, NULL);
}

void xMemFiller::mwr(int adr, int val) {
	rzx_rec_touch();
	comp_poke(comp, adr, val, conf.dbg.romwr);
}

void xMemFiller::fill() {
	QStringList strl = ui.leBytes->text().split(":",X_SkipEmptyParts);
	QStringList strm = ui.leMask->text().split(":",X_SkipEmptyParts);
	int psiz = strl.size();		// pattern size
	if (psiz == 0) return;
	unsigned char pat[8];		// pattern bytes
	unsigned char msk[8];		// mask bytes
	int idx = 0;
	for (idx = 0; idx < 8; idx++) {	// fill arrays
		if (idx < strl.size()) {
			pat[idx] = strl[idx].toInt(NULL, 16) & 0xff;
		} else {
			pat[idx] = 0x00;
		}
		if (idx < strm.size()) {
			msk[idx] = strm[idx].toInt(NULL, 16) & 0xff;
		} else {
			msk[idx] = 0xff;
		}
	}
	int adr = ui.leStartHex->getValue();	// start addr
	int end = ui.leEndHex->getValue();	// end addr
	unsigned char byt;
	idx = 0;
	do {				// fill by pattern and mask
		byt = mrd(adr & 0xffff) & 0xff;
		switch(ui.cbMethod->currentIndex()) {
			case 0:				// mask
				byt = (byt & ~msk[idx]) | (pat[idx] & msk[idx]);
				break;
			case 1:				// put
				byt = pat[idx];
				break;
			case 2:				// or
				byt |= pat[idx];
				break;
			case 3:				// and
				byt &= pat[idx];
				break;
			case 4:				// xor
				byt ^= pat[idx];
				break;
		}
		mwr(adr & 0xffff, byt);
		idx++;
		if (idx >= psiz)
			idx = 0;
		adr++;
	} while (adr <= end);
	emit rqRefill();
	close();
}
