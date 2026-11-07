//#include "dbg_widgets.h"
#include "dbg_cmos_dump.h"
#include "../../xcore/xcore.h"
#include "../xgui.h"

xCmosDumpModel::xCmosDumpModel(QObject *p):xTableModel(p) {
}

int xCmosDumpModel::rowCount(const QModelIndex&) const {
	return 32;
}

int xCmosDumpModel::columnCount(const QModelIndex&) const {
	return 9;
}

QVariant xCmosDumpModel::data(const QModelIndex& idx, int role) const {
	QVariant res;
	int row = idx.row();
	int col = idx.column();
	int adr = (row << 3) + col - 1;
	unsigned char* cm = conf.zx->cmos.data;
	switch(role) {
		case Qt::ToolTipRole:
			// the byte and the word read on from it, at the cell's place in the CMOS
			if ((col == 0) || !conf.dbg.numtips) break;
			res = dbg_byte_tip(cm[adr], (adr < 0xff) ? cm[adr] | (cm[adr + 1] << 8) : -1,
				QString("CMOS #%0").arg(gethexbyte(adr)));
			break;
		case Qt::TextAlignmentRole:
			if (col != 0) res = Qt::AlignCenter;
			break;
		case Qt::DisplayRole:
			if (col == 0) {
				res = gethexbyte(row << 3).prepend("#");		// 00..F8 = address
			} else {
				res = gethexbyte(conf.zx->cmos.data[(row << 3) + col - 1] & 0xff);
			}
			break;
		case Qt::EditRole:
			if (col == 0) break;
			res = gethexbyte(conf.zx->cmos.data[(row << 3) + col - 1] & 0xff);
			break;
	}
	return res;
}

bool xCmosDumpModel::setData(const QModelIndex& idx, const QVariant& val, int role) {
	int row = idx.row();
	int col = idx.column();
	if (col == 0) return false;
	int adr = (row << 3) + col - 1;
	if (adr > 0xff) return false;
	bool flag;
	int d;
	switch(role) {
		case Qt::EditRole:
			d = val.toString().toInt(&flag,16) & 0xff;
			if (flag) {
				conf.zx->cmos.data[adr] = d;
			}
			break;
	}
	return true;
}

Qt::ItemFlags xCmosDumpModel::flags(const QModelIndex& idx) const {
	Qt::ItemFlags f = QAbstractTableModel::flags(idx);
	if (idx.column() > 0)
		f |= Qt::ItemIsEditable;
	return f;
}

// table

xCmosDumpTable::xCmosDumpTable(QWidget* p):QTableView(p) {
	dbg_cell_tips(this);
}

void xCmosDumpTable::resizeEvent(QResizeEvent* e) {
	QTableView::resizeEvent(e);
	int w = e->size().width();
	int sz = w / 9;
	horizontalHeader()->setDefaultSectionSize(sz);
}

// widget

xCmosDumpWidget::xCmosDumpWidget(QString i, QString t, QWidget* p):xDockWidget(i,t,p) {
	QWidget* wid = new QWidget;
	setWidget(wid);
	ui.setupUi(wid);
	setObjectName("CMOSDUMPWIDGET");
	ui.tabCmos->setModel(new xCmosDumpModel());
}

void xCmosDumpWidget::draw() {
	ui.tabCmos->update();
	dbg_cell_tip_follow(ui.tabCmos);
}
