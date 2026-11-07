// TODO: extend labels list to labels sets manager, add address column to labels list

#include "labelist.h"
#include "xgui.h"

#include <QInputDialog>
#include <QDebug>

// model

xLabelistModel::xLabelistModel(QObject* p):QAbstractListModel(p) {
	cpuMode = false;
	romMode = false;
}

int xLabelistModel::rowCount(const QModelIndex&) const {
	return list.size();
}

QStringList filter(QStringList lst, QString f) {
	QString str;
	QStringList res;
	foreach(str, lst) {
		if (str.contains(f))
			res.append(str);
	}
	return res;
}

void xLabelistModel::reset(QString f) {
	// list = conf.labels.keys().filter(f, Qt::CaseInsensitive);
	fstr = f;
	xLabelSet* set = conf.curlabset;
	if (romMode) {
		list = filter(labels_rom_names(), f);
	} else if (set) {
		list = filter(set->list.keys(), f);
//		list.sort();		// useless
	} else {
		list.clear();
	}
	emit dataChanged(index(0,0), index(0, list.size()));
}

void xLabelistModel::setCpuMode(bool f) {
	cpuMode = f;
	reset(fstr);
}

QModelIndex xLabelistModel::index(int row, int col, const QModelIndex&) const {
	return createIndex(row, col, nullptr);
}

QVariant xLabelistModel::data(const QModelIndex& idx, int role) const {
	QVariant res;
	if (!idx.isValid()) return res;
	int row = idx.row();
	if (row < 0) return res;
	if (row >= rowCount()) return res;
	QString str;
	QString adrstr;
	bool pref = false;
	int zadr;
	xAdr xadr;
	switch (role) {
		case Qt::DisplayRole:
			str = list.at(row);
			xadr = romMode ? labels_rom_cell(str) : find_label(str);
			if (cpuMode) {
				zadr = (xadr.type == MEM_ROM) ? xadr.adr : comp_find_xadr(conf.zx, xadr.type, xadr.abs, MEM_VIEW_READ);
				if (zadr < 0) {
					if ((conf.zx->hw->id == HW_ZXNEXT) && (xadr.type == MEM_RAM) && (xadr.abs >= 0x40000))
						adrstr = QString("%0:%1").arg(gethexbyte((xadr.abs - 0x40000) >> 13), dbg_adr(xadr.abs & 0x1fff));
					else
						adrstr = QString("%0:%1").arg(gethexbyte(xadr.abs >> 14), dbg_adr(xadr.abs & 0x3fff));
					pref = true;
				} else {
					adrstr = QString("CPU:%0").arg(dbg_adr(zadr));
				}
			} else {
				adrstr = dbg_abs(xadr.abs);
				pref = true;
			}
			if (pref) {
				switch(xadr.type) {
					case MEM_RAM: adrstr.prepend("RAM:"); break;
					case MEM_ROM: adrstr.prepend("ROM:"); break;
					case MEM_SLOT: adrstr.prepend("SLT:"); break;
					case MEM_DIV: adrstr.prepend("DIV:"); break;
					case MEM_EXT: adrstr.prepend("EXT:"); break;
					case MEM_IO: adrstr = QString("CPU:%0").arg(dbg_adr(xadr.adr));
				}
			}
			str.prepend(" | ").prepend(adrstr);
			res = str;
			break;
	}
	return res;
}

// window

void setRFIndex(QComboBox*, QVariant, int);
QString getRFSData(QComboBox*);

xLabeList::xLabeList(QWidget* p):QDialog(p) {
	ui.setupUi(this);
	mod = new xLabelistModel();
	ui.list->setModel(mod);
	romIdx = -1;

	connect(ui.name, SIGNAL(textChanged(QString)), mod, SLOT(reset(QString)));
	// a double click or Enter, as the platform has it; a double click also emits
	// doubleClicked, and listening to both put the label in twice
	connect(ui.list, SIGNAL(activated(QModelIndex)), this, SLOT(listDoubleClicked(QModelIndex)));
	connect(ui.cbLabelSet, SIGNAL(currentIndexChanged(int)), this, SLOT(changeLabelSet()));

	connect(ui.tbAddGrp, SIGNAL(released()), this, SLOT(newGroup()));
	connect(ui.tbEditGrp, SIGNAL(released()), this, SLOT(editGroup()));
	connect(ui.tbDelGrp, SIGNAL(released()), this, SLOT(delGroup()));
	connect(ui.cbAdrCpu, SIGNAL(clicked(bool)), mod, SLOT(setCpuMode(bool)));
}

void xLabeList::listDoubleClicked(QModelIndex idx) {
	if (!idx.isValid()) return;
	QString str = mod->list.at(idx.row());
	if (mod->romMode) {		// a label of the user's of that name is not the one picked
		xAdr xadr = label_home(labels_rom_cell(str));	// paged out: where its ROM runs
		if (xadr.adr >= 0) emit adrSelected(xadr.adr);
		return;
	}
	emit labSelected(str);
//	close();
}

// the ROM labels are only looked at: the user's set stays in effect
void xLabeList::changeLabelSet() {
	bool rom = (romIdx >= 0) && (ui.cbLabelSet->currentIndex() == romIdx);
	setRomMode(rom);
	if (rom) {
		mod->reset(ui.name->text());
		return;
	}
	QString str = getRFSData(ui.cbLabelSet);
	setLabelSet(str);
	mod->reset(ui.name->text());
	emit labSetChanged();
}

// the ROM labels are only looked at: no editing the set
void xLabeList::setRomMode(bool rom) {
	mod->romMode = rom;
	ui.tbEditGrp->setEnabled(!rom);
	ui.tbDelGrp->setEnabled(!rom);
}

void xLabeList::newGroup() {
	QString str = QInputDialog::getText(this, "Input name", "Set name");
	if (str.isEmpty()) return;
	newLabelSet(str);
	setLabelSet(str);
	fillSetList();
	mod->reset(ui.name->text());
}

void xLabeList::editGroup() {
	xLabelSet* set = conf.curlabset;
	if (!set) return;
	QString str = QInputDialog::getText(this, "Input name", "Set name",QLineEdit::Normal,set->name);
	if (str.isEmpty()) return;
	set->name = str;
	fillSetList();
	mod->reset(ui.name->text());
}

void xLabeList::delGroup() {
	xLabelSet* set = conf.curlabset;
	if (!set) return;
	if (!areSure("Do you want to delete this labelset?")) return;
	delLabelSet(set->name);
	fillSetList();
	mod->reset(ui.name->text());
	emit labSetChanged();		// to refill disasm
}

void xLabeList::fillSetList() {
	ui.cbLabelSet->blockSignals(true);		// prevent index changing -> slot calling -> current labset changing
	ui.cbLabelSet->clear();
	foreach(xLabelSet* set, conf.labsets) {
		ui.cbLabelSet->addItem(set->name, set->name);
	}
	romIdx = -1;
	if (!labels_rom_names().isEmpty()) {
		romIdx = ui.cbLabelSet->count();
		ui.cbLabelSet->addItem("ROM labels (read-only)", QString());
	}
	setRomMode(false);
	if (conf.curlabset) {
		setRFIndex(ui.cbLabelSet, conf.curlabset->name, 0);
	} else {
		ui.cbLabelSet->setCurrentIndex(-1);
	}
	ui.cbLabelSet->blockSignals(false);
	emit labSetChanged();
}

void xLabeList::show() {
	fillSetList();
	mod->reset(ui.name->text());
	ui.name->setFocus();
	QWidget::show();
}
