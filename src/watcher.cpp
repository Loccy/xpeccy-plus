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
// the fields as the debugger has them; again only when its numbers or Split
// pairs moved - every frame would repolish them and put the caret back to 0
void xWatcher::setupRegs(const xRegBunch& regs) {
	setupKey = conf.dbg.numfmt * 2 + conf.dbg.regsplit;
	for (int i = 0; (i < regValues.size()) && (regs.regs[i].id != REG_EOT); i++)
		reg_field_setup(regValues[i], regs.regs[i].size, regs.regs[i].flag, regs.regs[i].name);
}

void xWatcher::setRegs(const xRegBunch& regs) {
	if (setupKey != conf.dbg.numfmt * 2 + conf.dbg.regsplit) setupRegs(regs);
	for (int i = 0; (i < regValues.size()) && (regs.regs[i].id != REG_EOT); i++) {
		if (regs.regs[i].size == REG_BIT) {
			regBits[i]->setChecked(regs.regs[i].value);
		} else {
			regValues[i]->setValue(regs.regs[i].value);
		}
	}
}

xWatcher::xWatcher(QWidget* p):QDialog(p) {
	int i;
	ui.setupUi(this);
	model = new xWatchModel;
	ui.wchMemTab->setModel(model);
	ui.wchMemTab->addAction(ui.actAddWatcher);
	ui.wchMemTab->addAction(ui.actDelWatcher);
	// never scrolled across: a moment too narrow would leave its left side cut off
	QScrollBar* hsb = ui.wchMemTab->horizontalScrollBar();
	connect(hsb, &QScrollBar::valueChanged, this, [hsb](int v) {
		if (v != 0) hsb->setValue(0);
	});

// like in the debugger: pairs regName/regValue, in its numbers. No changed
// marks: the values move every frame here
	QLabel* lp;
	xHexSpin* hp;
	for(i = 0; i < 32; i++) {
		lp = new QLabel(this);		// the window's from the start: the grid takes them later
		lp->setContentsMargins(0, 0, 4, 0);	// don't let it touch the value
		hp = new xHexSpin(this);
		hp->setXFlag(XHS_DEC | XHS_FILL | XHS_AUTOW | XHS_NUMS);
		hp->setFrame(false);
		hp->setAlignment(Qt::AlignCenter);
		// a register of one bit (IFF1, IFF2) is a mark, as in the debugger;
		// it shows the state, the watcher changes nothing
		QCheckBox* cb = new QCheckBox(this);
		cb->setAttribute(Qt::WA_TransparentForMouseEvents);
		cb->setFocusPolicy(Qt::NoFocus);
		cb->setVisible(false);
		lp->setVisible(false);
		hp->setVisible(false);
		regLabels.append(lp);
		regValues.append(hp);
		regBits.append(cb);
	}				// placeRegs() puts them into the grid
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

// The rows of the debugger's two-column layout: a register, and beside it the
// one the CPU's table pairs it with - PC SP, AF AF', ..., IR IM, IFF1 IFF2
void xWatcher::placeRegs(const xRegBunch& regs) {
	QLayoutItem* item;
	while ((item = ui.regGrid->takeAt(0)) != nullptr)
		delete item;		// the widgets stay, only their places go
	int cnt = 0;
	while ((cnt < regLabels.size()) && (regs.regs[cnt].id != REG_EOT))
		cnt++;
	QVector<char> done(cnt, 0);
	int row = 0;
	auto put = [this](int i, int row, int col) {
		ui.regGrid->addWidget(regLabels[i], row, col * 2);
		ui.regGrid->addWidget(regValues[i], row, col * 2 + 1);
		ui.regGrid->addWidget(regBits[i], row, col * 2 + 1);
	};
	for (int i = 0; i < cnt; i++) {
		if (done[i]) continue;		// already beside its pair
		done[i] = 1;
		put(i, row, 0);
		for (int j = 0; (j < cnt) && regs.regs[i].pair; j++) {
			if (!done[j] && (regs.regs[j].id == regs.regs[i].pair)) {
				done[j] = 1;
				put(j, row, 1);
				break;
			}
		}
		row++;
	}
}

void xWatcher::show() {
	layoutRegs(conf.zx->cpu);
	updateStyle();			// the font may have been read after this window was made
	QDialog::show();
}

// the CPU's registers: names, places, fields; again when another CPU comes
void xWatcher::layoutRegs(CPU* cpu) {
	regCore = cpu->core;
	xRegBunch regs = cpuGetRegs(cpu);
	placeRegs(regs);
	setupRegs(regs);
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
	if (comp->cpu->core != regCore) layoutRegs(comp->cpu);
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

// The last cell is the corner, not one past it: Qt 6 repaints only the cells of the
// range, and a corner outside the table left the bytes as they were drawn first.
void xWatchModel::update() {
	if (!rowCount()) return;
	emit QAbstractItemModel::dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
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
	// a watch is two rows, said before they are there
	int row = explist.size() * 2;
	beginInsertRows(QModelIndex(), row, row + 1);
	explist.append(itm);
	endInsertRows();
}

void xWatchModel::setItem(int idx, int type, QString exp) {
	if (idx < 0) return;
	if (idx >= explist.size()) return;
	xWatchItem itm;
	itm.type = type;
	itm.exp = exp;
	explist[idx] = itm;
	emit QAbstractItemModel::dataChanged(index(idx * 2, 0), index(idx * 2 + 1, columnCount() - 1));	// its two rows
}

void xWatchModel::delItem(int idx) {
	if ((idx < 0) || (idx >= explist.size())) return;
	beginRemoveRows(QModelIndex(), idx * 2, idx * 2 + 1);
	explist.removeAt(idx);
	endRemoveRows();
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
