#include "watcher.h"
#include "xcore/xcore.h"
#include "xgui/debuga/dbg_dump.h"

#include <QInputDialog>
#include <QHeaderView>
#include <QScrollBar>
#include <QMenu>
#include <QApplication>
#include <QDebug>

// expression evaluation moved to xcore/xexpr.cpp

// wutcha

extern QString getStyleString(QString, QString, int = 0, int = 100);

// the two headers follow the debugger's own, so a style with a .pal of its own
// reaches them too. The font is the debugger's: this window is not one of its
// children, so it takes it here, and the columns are measured in it

void xWatcher::updateStyle() {
	QString str = getStyleString("dbg.header.bg", "dbg.header.txt");
	ui.label_15->setStyleSheet(str);
	ui.label_14->setStyleSheet(str);
	QFont uiFont = QApplication::font();
	foreach (QWidget* wid, findChildren<QWidget*>())
		wid->setFont(qobject_cast<QMenu*>(wid) ? uiFont : conf.dbg.font);
	setFont(conf.dbg.font);
	foreach (xHexSpin* xhs, regValues)
		xhs->updatePal();
	fitColumns();
}

// a byte a column, as the DUMP panel has them: the cells are aligned right and
// the first of a group of four is wider, so the groups stand apart. The
// address of a watch spans the last three
void xWatcher::fitColumns() {
	QFontMetrics fm(conf.dbg.font);
	int w = dump_cell_width(fm);
	int all = 0;
	QHeaderView* hdr = ui.wchMemTab->horizontalHeader();
	hdr->setMinimumSectionSize(w);
	hdr->setDefaultSectionSize(w);
	for (int i = 0; i < WCH_BYTES; i++) {
		int cw = w + (((i > 0) && !(i % DUMP_GROUP)) ? DUMP_GAP : 0);
		ui.wchMemTab->setColumnWidth(i, cw);
		all += cw;
	}
	// room for all of them, and for the scroll bar a long list brings; never one
	// across, the last column takes what is left
	ui.wchMemTab->setMinimumWidth(all + ui.wchMemTab->frameWidth() * 2
		+ ui.wchMemTab->verticalScrollBar()->sizeHint().width() + 4);
	ui.wchMemTab->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	int h = fm.height() + 4;
	ui.wchMemTab->verticalHeader()->setMinimumSectionSize(h);
	ui.wchMemTab->verticalHeader()->setDefaultSectionSize(h);
}

// the registers as the debugger shows them: its base, and a pair lit (or, in
// decimal, written) byte by byte with Split pairs
void xWatcher::setRegs(const xRegBunch& regs) {
	for (int i = 0; (i < regValues.size()) && (regs.regs[i].id != REG_EOT); i++) {
		int mx;
		switch (regs.regs[i].size) {
			case REG_2: mx = 2; break;
			case REG_BIT: mx = 1; break;
			case REG_BYTE: mx = 0xff; break;
			case REG_24: mx = 0xffffff; break;
			case REG_32: mx = 0x7fffffff; break;
			default: mx = 0xffff; break;
		}
		if (regs.regs[i].size == REG_BIT) {
			regBits[i]->setChecked(regs.regs[i].value);
			continue;
		}
		regValues[i]->setMax(mx);
		regValues[i]->numFmt();
		regValues[i]->setSplit(conf.dbg.regsplit && !(regs.regs[i].flag & REG_TYPE_M));
		regValues[i]->refitWidth();	// a word as wide as a split pair, as in the debugger
		regValues[i]->setValue(regs.regs[i].value);
	}
}

xWatcher::xWatcher(QWidget* p):QDialog(p) {
	int i;
	ui.setupUi(this);
	model = new xWatchModel;
	ui.wchMemTab->setModel(model);
	ui.wchMemTab->addAction(ui.actAddWatcher);
	ui.wchMemTab->addAction(ui.actDelWatcher);
	// it is never scrolled across: a moment of being too narrow (the registers
	// beside it growing, decimal) would leave it scrolled, its left side cut off
	QScrollBar* hsb = ui.wchMemTab->horizontalScrollBar();
	connect(hsb, &QScrollBar::valueChanged, this, [hsb](int v) {
		if (v != 0) hsb->setValue(0);
	});

// like in the debugger: pairs regName/regValue, in its numbers. No changed
// marks: the values move every frame here
	QLabel* lp;
	xHexSpin* hp;
	for(i = 0; i < 32; i++) {
		lp = new QLabel;
		lp->setContentsMargins(0, 0, 4, 0);	// don't let it touch the value
		hp = new xHexSpin;
		hp->setXFlag(XHS_DEC | XHS_FILL | XHS_AUTOW | XHS_NUMS);
		hp->setFrame(false);
		hp->setAlignment(Qt::AlignCenter);
		// a register of one bit (IFF1, IFF2) is a mark, as in the debugger;
		// it shows the state, the watcher changes nothing
		QCheckBox* cb = new QCheckBox;
		cb->setAttribute(Qt::WA_TransparentForMouseEvents);
		cb->setFocusPolicy(Qt::NoFocus);
		cb->setVisible(false);
		regLabels.append(lp);
		regValues.append(hp);
		regBits.append(cb);
		ui.regGrid->addWidget(lp, i >> 1, (i & 1) << 1);
		ui.regGrid->addWidget(hp, i >> 1, ((i & 1) << 1) | 1);
		ui.regGrid->addWidget(cb, i >> 1, ((i & 1) << 1) | 1);
	}
//	ui.regGrid->setRowStretch(32, 10);

	labswin = new xLabeList;

	newWch = new QDialog(this);
	nui.setupUi(newWch);
	nui.cbType->addItem("CPU addr", WUT_CPU);
	nui.cbType->addItem("RAM addr", WUT_RAM);
	nui.cbType->addItem("ROM addr", WUT_ROM);
	connect(nui.tbLabel, SIGNAL(released()), labswin, SLOT(show()));
	label_complete(nui.leExpression);
	connect(labswin, SIGNAL(labSelected(QString)), this, SLOT(insertLabel(QString)));

	updateStyle();

	connect(ui.actAddWatcher, SIGNAL(triggered(bool)), this, SLOT(newWatcher()));
	connect(ui.actDelWatcher, SIGNAL(triggered(bool)), this, SLOT(delWatcher()));
	connect(ui.wchMemTab, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(edtWatcher()));

	connect(nui.pbOK, SIGNAL(released()), this, SLOT(confirmNew()));
}

void xWatcher::show() {

	xRegBunch regs = cpuGetRegs(conf.zx->cpu);
	int work = 1;
	for(int i = 0; i < 32; i++) {
		if (work) {
			if (regs.regs[i].id == REG_EOT) {
				work = 0;
				regLabels[i]->setVisible(false);
				regValues[i]->setVisible(false);
				regBits[i]->setVisible(false);
			} else {
				bool bit = (regs.regs[i].size == REG_BIT);
				regLabels[i]->setVisible(true);
				regValues[i]->setVisible(!bit);
				regBits[i]->setVisible(bit);
				regLabels[i]->setText(regs.regs[i].name);
				regLabels[i]->setToolTip(dbg_reg_tip(regs.regs[i].name));
			}
		} else {
			regLabels[i]->setVisible(false);
			regValues[i]->setVisible(false);
			regBits[i]->setVisible(false);
		}
	}
	setRegs(regs);
	updateStyle();			// the font may have been read after this window was made
	QDialog::show();
}

QString getBankType(int type) {
	QString res;
	switch(type) {
		case MEM_ROM: res = "ROM"; break;
		case MEM_RAM: res = "RAM"; break;
		case MEM_SLOT: res = "SLT"; break;
		case MEM_DIV: res = "DIV"; break;
		default: res = "EXT"; break;
	}
	return res;
}

QString getBankName(MemPage pg) {
	return QString("%0:%1").arg(getBankType(pg.type), gethexbyte(pg.num >> 6));
}

void xWatcher::fillFields(Computer* comp) {
	if (!isVisible()) return;
	if (comp == NULL) return;
	model->comp = comp;

	setRegs(cpuGetRegs(comp->cpu));
	ui.wchBank0->setText(getBankName(comp->mem->map[0x00]));
	ui.wchBank1->setText(getBankName(comp->mem->map[0x40]));
	ui.wchBank2->setText(getBankName(comp->mem->map[0x80]));
	ui.wchBank3->setText(getBankName(comp->mem->map[0xc0]));

	model->update();
}

int xWatcher::getCurRow() {
	int res = -1;
	QModelIndexList lst = ui.wchMemTab->selectionModel()->selectedRows();
	if (lst.size() == 1)
		res = lst.first().row() / 2;
	return res;
}

void xWatcher::newWatcher() {
	curwch = -1;
	newWch->show();
}

void xWatcher::confirmNew() {
	int type = nui.cbType->currentData().toInt();
	QString str = nui.leExpression->text();
	if (str.isEmpty()) return;
	xResult res = xEval(str.toLocal8Bit().data(), type);		// check syntax
	if (res.err) return;
	newWch->close();
	if (curwch < 0) {
		model->addItem(type, str);
		for (int i = 0; i < model->getItemCount() * 2; i += 2) {
			ui.wchMemTab->setSpan(i, 0, 1, WCH_BYTES - 3);
			ui.wchMemTab->setSpan(i, WCH_BYTES - 3, 1, 3);
		}
		fitColumns();
	} else {
		model->setItem(curwch, type, str);
	}
}

void xWatcher::insertLabel(QString lab) {
	nui.leExpression->insert(lab);
}

void xWatcher::delWatcher() {
	int row = getCurRow();
	if (row < 0) return;
	model->delItem(row);
}

void xWatcher::edtWatcher() {
	curwch = getCurRow();
	if (curwch < 0) return;
	xWatchItem itm = model->getItem(curwch);
	nui.cbType->setCurrentIndex(nui.cbType->findData(itm.type));
	nui.leExpression->setText(itm.exp);
	newWch->show();
}

// watcher view model

xWatchModel::xWatchModel() {
}

void xWatchModel::update() {
	emit QAbstractItemModel::dataChanged(index(0, 0), index(rowCount(), columnCount()));
}

QModelIndex xWatchModel::index(int row, int col, const QModelIndex&) const {
	QModelIndex res = createIndex(row, col, (void*)this);
	return res;
}

QModelIndex xWatchModel::parent(const QModelIndex&) const {
	return QModelIndex();
}

int xWatchModel::rowCount(const QModelIndex&) const {
	return explist.size() * 2;
}

int xWatchModel::columnCount(const QModelIndex&) const {
	return WCH_BYTES + 1;		// and an empty one after them that takes the slack
}

void xWatchModel::insertRow(int row, const QModelIndex& idx) {
	emit beginInsertRows(idx,row,row);
	emit endInsertRows();
}

void xWatchModel::removeRow(int row, const QModelIndex& idx) {
	emit beginRemoveRows(idx,row*2,row*2+1);
	emit endRemoveRows();
}

int xWatchModel::getItemCount() {
	return explist.size();
}

xWatchItem xWatchModel::getItem(int row) {
	return explist.at(row);
}

void xWatchModel::addItem(int type, QString exp) {
	xWatchItem itm;
	itm.type = type;
	itm.exp = exp;
	explist.append(itm);
	insertRow(explist.size() - 1);
	insertRow(explist.size() - 1);
}

void xWatchModel::setItem(int idx, int type, QString exp) {
	if (idx < 0) return;
	if (idx >= explist.size()) return;
	xWatchItem itm;
	itm.type = type;
	itm.exp = exp;
	explist[idx] = itm;
	emit QAbstractItemModel::dataChanged(index(idx, 0), index(idx, columnCount()));
}

void xWatchModel::delItem(int idx) {
	if (idx < explist.size()) {
		explist.removeAt(idx);
		removeRow(idx);
	}
}

QVariant xWatchModel::data(const QModelIndex& idx, int role) const {
	QVariant res;
	if (!idx.isValid()) return res;
	int row = idx.row();
	int col = idx.column();
	if ((row < 0) || (row >= rowCount(idx))) return res;
	if ((col < 0) || (col >= columnCount(idx))) return res;
	xWatchItem itm;
	xResult xr;
	if (col >= WCH_BYTES) return res;		// the empty one
	switch (role) {
		case Qt::TextAlignmentRole:
			// the bytes, and the address of a watch, stand to the right of their cells
			if ((row & 1) || (col == WCH_BYTES - 3))
				res = (int)(Qt::AlignRight | Qt::AlignVCenter);
			break;
		case Qt::DisplayRole:
			itm = explist.at(row >> 1);
			xr = xEval(itm.exp.toLocal8Bit().data(), itm.type);
			if (row & 1) {
				if (xr.err) {
					res = "??";
				} else {
					int adr = xr.value + col;
					int byte;
					if (itm.type == WUT_RAM) byte = comp->mem->ramData[adr & comp->mem->ramMask];
					else if (itm.type == WUT_ROM) byte = comp->mem->romData[adr & comp->mem->romMask];
					else byte = comp_peek(comp, adr, MEM_VIEW_READ, NULL);
					res = gethexbyte(byte);
				}
			} else if (col == 0) {
				switch(itm.type) {
					case WUT_CPU: res = "CPU: "+itm.exp; break;
					case WUT_RAM: res = "RAM: "+itm.exp; break;
					case WUT_ROM: res = "ROM: "+itm.exp; break;
					default: res = "Error"; break;
				}
			} else if (col == WCH_BYTES - 3) {
				if (xr.err) {
					res = "????";
				} else {
					res = (itm.type == WUT_CPU) ? dbg_adr(xr.value) : dbg_abs(xr.value);
				}
			}
	}
	return res;
}
