#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QScrollBar>
#include <QSet>
#include <QVBoxLayout>

#include "cheatwin.h"
#include "../xcore/xcore.h"
#include "../xcore/cheats.h"

enum {CW_CHECK = 0, CW_NAME, CW_VALUE, CW_NOTE};

static const char* cw_bad[] = {"", "a line that does not read", "a number out of range",
	"a line that is not a poke", "no last poke (Z)", "no pokes"};

xCheatWin::xCheatWin(QWidget* p) : QDialog(p) {
	setWindowTitle("Cheats");
	setObjectName("cheatWin");
	setWindowIcon(QIcon(":/images/cheats.png"));
	serial = -1;
	rzx = false;

	labFile = new xElideLabel;
	labFile->setObjectName("cheatFile");
	QPushButton* btnOpen = new QPushButton(QIcon(":/images/fileopen.png"), "Open...");
	btnClear = new QPushButton(QIcon(":/images/cancel.png"), "Clear");
	btnOpen->setAutoDefault(false);
	btnClear->setAutoDefault(false);
	connect(btnOpen, &QPushButton::clicked, this, [this]() {
		QString path = QFileDialog::getOpenFileName(this, "Open cheats", QString::fromLocal8Bit(conf.lastDir.c_str()),
			"Cheats (*.pok *.POK);;All files (*)");
		if (path.isEmpty()) return;
		conf.lastDir = std::string(QFileInfo(path).dir().absolutePath().toLocal8Bit().data());
		openFile(path);
	});
	connect(btnClear, &QPushButton::clicked, this, [this]() {
		cheats_clear();
		note(QString());
	});
	QHBoxLayout* top = new QHBoxLayout;
	top->addWidget(labFile, 1);
	top->addWidget(btnOpen);
	top->addWidget(btnClear);

	tree = new QTreeWidget;
	tree->setObjectName("cheatList");
	tree->setColumnCount(4);
	tree->setHeaderLabels(QStringList() << "" << "Trainer" << "Value" << "");
	tree->setItemDelegateForColumn(CW_CHECK, new xCheckItem(tree));
	tree->setSelectionMode(QAbstractItemView::NoSelection);
	tree->setFocusPolicy(Qt::NoFocus);
	tree->header()->setStretchLastSection(false);
	// fixed: a trainer's pokes sit one step further in, and would widen it when opened
	tree->header()->setSectionResizeMode(CW_CHECK, QHeaderView::Fixed);
	tree->header()->setMinimumSectionSize(1);
	tree->setColumnWidth(CW_CHECK, tree->indentation() + style()->pixelMetric(QStyle::PM_IndicatorWidth) + 6);
	tree->header()->setSectionResizeMode(CW_NAME, QHeaderView::Stretch);
	tree->header()->setSectionResizeMode(CW_VALUE, QHeaderView::ResizeToContents);
	tree->header()->setSectionResizeMode(CW_NOTE, QHeaderView::Fixed);	// to its text and a margin, in fill()
	tree->setMinimumSize(300, 160);
	connect(tree, &QTreeWidget::itemClicked, this, &xCheatWin::onClick);

	labNote = new QLabel;
	labNote->setWordWrap(true);
	labNote->hide();

	// by hand: decimal first, as BASIC's POKE takes it, and the same in hex under it
	QGroupBox* box = new QGroupBox("Custom POKEs");
	box->setObjectName("cheatHand");
	adrDec = new QLineEdit;
	adrHex = new QLineEdit;
	valDec = new QLineEdit;
	valHex = new QLineEdit;
	adrDec->setObjectName("cheatAdrDec");
	adrHex->setObjectName("cheatAdrHex");
	valDec->setObjectName("cheatValDec");
	valHex->setObjectName("cheatValHex");
	int w = adrDec->fontMetrics().horizontalAdvance("000000") + 16;
	foreach(QLineEdit* ed, QList<QLineEdit*>() << adrDec << adrHex << valDec << valHex)
		ed->setFixedWidth(w);
	tie(adrDec, adrHex, 0xffff, 4);
	tie(valDec, valHex, 0xff, 2);
	setTabOrder(adrDec, valDec);
	setTabOrder(valDec, adrHex);
	setTabOrder(adrHex, valHex);
	btnPoke = new QPushButton("Poke");
	btnPoke->setObjectName("cheatPoke");
	btnPoke->setAutoDefault(false);
	btnPoke->setEnabled(false);
	connect(btnPoke, &QPushButton::clicked, this, &xCheatWin::pokeByHand);
	// Enter in the address goes on to the value; in the value it pokes
	foreach(QLineEdit* ed, QList<QLineEdit*>() << adrDec << adrHex)
		connect(ed, &QLineEdit::returnPressed, this, [this, ed]() {
			QLineEdit* to = (ed == adrHex) ? valHex : valDec;
			to->setFocus();
			to->selectAll();
		});
	foreach(QLineEdit* ed, QList<QLineEdit*>() << valDec << valHex)
		connect(ed, &QLineEdit::returnPressed, this, &xCheatWin::pokeByHand);
	QLabel* labAdr = new QLabel("Address");
	QLabel* labVal = new QLabel("Value");
	QLabel* labDec = new QLabel("DEC");
	QLabel* labHex = new QLabel("HEX");
	labAdr->setAlignment(Qt::AlignCenter);
	labVal->setAlignment(Qt::AlignCenter);
	QGridLayout* grid = new QGridLayout(box);
	grid->setVerticalSpacing(4);
	grid->addWidget(labAdr, 0, 1);
	grid->addWidget(labVal, 0, 2);
	grid->addWidget(labDec, 1, 0);
	grid->addWidget(adrDec, 1, 1);
	grid->addWidget(valDec, 1, 2);
	grid->addWidget(labHex, 2, 0);
	grid->addWidget(adrHex, 2, 1);
	grid->addWidget(valHex, 2, 2);
	grid->setColumnStretch(3, 1);
	grid->addWidget(btnPoke, 2, 4);
	// the row without the focus only follows the other: shown a little dimmer
	decSide << labDec << adrDec << valDec;
	hexSide << labHex << adrHex << valHex;
	foreach(QWidget* wid, decSide + hexSide) {
		QGraphicsOpacityEffect* eff = new QGraphicsOpacityEffect(wid);
		eff->setOpacity(0.6);
		wid->setGraphicsEffect(eff);
	}
	foreach(QLineEdit* ed, QList<QLineEdit*>() << adrDec << adrHex << valDec << valHex)
		ed->installEventFilter(this);
	dimSide(true);

	QPushButton* btnClose = new QPushButton(QIcon(":/images/cancel.png"), "Close");
	btnClose->setAutoDefault(false);
	connect(btnClose, &QPushButton::clicked, this, &QDialog::close);
	QHBoxLayout* bottom = new QHBoxLayout;
	bottom->addStretch(1);
	bottom->addWidget(btnClose);

	QVBoxLayout* lay = new QVBoxLayout(this);
	lay->addLayout(top);
	lay->addWidget(tree, 1);
	lay->addWidget(labNote);
	lay->addWidget(box);
	lay->addLayout(bottom);
}

// the two fields of a number in step: the one typed in rewrites the other
void xCheatWin::tie(QLineEdit* dec, QLineEdit* hex, int max, int digits) {
	auto check = [this]() {
		btnPoke->setEnabled((fieldValue(adrDec, 0xffff) >= 0) && (fieldValue(valDec, 0xff) >= 0));
	};
	connect(dec, &QLineEdit::textEdited, this, [=](const QString& txt) {
		bool ok;
		int v = txt.trimmed().toInt(&ok, 10);
		hex->setText((ok && (v >= 0) && (v <= max)) ? QString("%0").arg(v, digits, 16, QChar('0')).toUpper() : QString());
		check();
	});
	connect(hex, &QLineEdit::textEdited, this, [=](const QString& txt) {
		QString t = txt.trimmed();
		if (t.startsWith("#") || t.startsWith("$")) t = t.mid(1);
		else if (t.startsWith("0x", Qt::CaseInsensitive)) t = t.mid(2);
		bool ok;
		int v = t.toInt(&ok, 16);
		dec->setText((ok && (v >= 0) && (v <= max)) ? QString::number(v) : QString());
		check();
	});
}

bool xCheatWin::eventFilter(QObject* obj, QEvent* ev) {
	if (ev->type() == QEvent::FocusIn)
		dimSide(decSide.contains(static_cast<QWidget*>(obj)));
	return QDialog::eventFilter(obj, ev);
}

void xCheatWin::dimSide(bool hex) {
	foreach(QWidget* wid, decSide)
		wid->graphicsEffect()->setEnabled(!hex);
	foreach(QWidget* wid, hexSide)
		wid->graphicsEffect()->setEnabled(hex);
}

int xCheatWin::fieldValue(QLineEdit* dec, int max) {
	bool ok;
	int v = dec->text().trimmed().toInt(&ok, 10);
	return (ok && (v >= 0) && (v <= max)) ? v : -1;
}

void xCheatWin::note(const QString& txt) {
	labNote->setText(txt);
	labNote->setVisible(!txt.isEmpty());
}

void xCheatWin::pokeByHand() {
	int adr = fieldValue(adrDec, 0xffff);
	int val = fieldValue(valDec, 0xff);
	if ((adr < 0) || (val < 0)) return;
	int res = cheats_hand_poke(conf.zx, adr, val);
	if (res == CHT_RZX) {
		note("An RZX is playing: nothing goes in while it plays.");
		return;
	}
	if (res != POK_FITS) {
		note(QString("No RAM at %0 (#%1): nothing poked.").arg(adr).arg(adr, 4, 16, QChar('0')).toUpper());
		return;
	}
	note(QString());
	// on to the next address, the value ready to be typed over
	adr = (adr + 1) & 0xffff;
	adrDec->setText(QString::number(adr));
	adrHex->setText(QString("%0").arg(adr, 4, 16, QChar('0')).toUpper());
	QLineEdit* to = (focusWidget() == valHex) ? valHex : valDec;
	to->setFocus();
	to->selectAll();
}

bool xCheatWin::openFile(const QString& path) {
	int err = cheats_open(path, false);
	if (err == POK_OK) {
		note(QString());
		return true;
	}
	QString why;
	switch (err) {
		case POK_ERR_EMPTY: why = "There are no trainers in it: it is not a .pok file."; break;
		case POK_ERR_SIZE: why = "It is too big for a .pok file."; break;
		case POK_ERR_MEM: why = "Out of memory."; break;
		default: why = "It can't be read."; break;
	}
	QMessageBox::warning(this, "Cheats", QString("%0\n\n%1").arg(QFileInfo(path).fileName(), why));
	return false;
}

bool xCheatWin::askValue(int idx, const QString& name) {
	bool ok;
	QString txt = QInputDialog::getText(this, "Cheats", QString("%0: the value to poke (0-255, #hex)").arg(name),
		QLineEdit::Normal, QString(), &ok);
	if (!ok) return false;
	int v = str_num(txt, &ok);
	if (!ok || (v < 0) || (v > 255)) {
		note("The value is a number from 0 to 255.");
		return false;
	}
	cheats_set_value(idx, v);
	return true;
}

void xCheatWin::onClick(QTreeWidgetItem* it, int col) {
	if (!it || it->parent()) return;
	int idx = it->data(CW_NAME, Qt::UserRole).toInt();
	bool on = it->data(CW_CHECK, Qt::CheckStateRole).toInt() != Qt::Unchecked;
	bool ask = it->data(CW_VALUE, Qt::UserRole).toBool();
	QString name = it->text(CW_NAME);
	if ((col == CW_VALUE) && ask && !on) {
		askValue(idx, name);
		return;
	}
	if (col != CW_CHECK) return;
	if (!on && ask && (it->data(CW_VALUE, Qt::UserRole + 1).toInt() < 0) && !askValue(idx, name))
		return;
	int res = cheats_set(conf.zx, idx, !on, false);
	if (res == POK_MISMATCH) {
		QString q = QString("\"%0\" does not match this release: the bytes it expects to change are not in memory.\n\n"
			"Poke anyway?").arg(name);
		if (areSure(q.toUtf8().constData()))
			res = cheats_set(conf.zx, idx, true, true);
		else
			res = POK_FITS;
	}
	switch (res) {
		case CHT_RZX: note("An RZX is playing: nothing goes in while it plays."); break;
		case POK_NO_PAGE: note(QString("\"%0\" pokes a 128K page this machine does not have.").arg(name)); break;
		case POK_NO_RAM: note(QString("\"%0\" pokes where this machine has no RAM now.").arg(name)); break;
		default: note(QString()); break;
	}
	fill();
}

static QString cw_poke_text(const xPoke& pk) {
	QString where = QString("%0 #%1").arg(pk.adr).arg(pk.adr, 4, 16, QChar('0')).toUpper();
	if (!(pk.bank & 8)) where = QString("page %0: %1").arg(pk.bank & 7).arg(where);
	QString val = (pk.val > 255) ? QString("?") : QString::number(pk.val);
	QString txt = QString("%0 = %1").arg(where, val);
	if (pk.org) txt += QString("  (was %0)").arg(pk.org);
	return txt;
}

void xCheatWin::fill() {
	serial = cheats_serial();
	QString path = cheats_file();
	labFile->setFull(path.isEmpty() ? QString("No .pok file") :
		QFileInfo(path).fileName() + (cheats_picked() ? "  (found beside the image)" : ""));
	labFile->setToolTip(QDir::toNativeSeparators(path));
	QList<xCheatRow> rows = cheats_rows(conf.zx);
	int count = cheats_count();
	btnClear->setEnabled(!rows.isEmpty());
	// kept as the user left them: which trainers are open, and where the list was
	QSet<int> open;
	for (int i = 0; i < tree->topLevelItemCount(); i++)
		if (tree->topLevelItem(i)->isExpanded()) open.insert(tree->topLevelItem(i)->data(CW_NAME, Qt::UserRole).toInt());
	int scroll = tree->verticalScrollBar()->value();
	tree->clear();
	QColor grey = palette().color(QPalette::Disabled, QPalette::Text);
	QColor warn = conf.pal.value(DBG_PAL_CHG_TXT, QColor(Qt::red));
	for (int i = 0; i < rows.size(); i++) {
		const xCheatRow& r = rows[i];
		int idx = (i < count) ? i : -1;
		QTreeWidgetItem* it = new QTreeWidgetItem;
		it->setFlags(Qt::ItemIsEnabled);	// the click is ours: a checkable item would flip itself first
		it->setData(CW_NAME, Qt::UserRole, idx);
		it->setText(CW_NAME, (idx < 0) ? QString("Custom (%0)").arg(r.pokes.size()) : r.name);
		it->setData(CW_VALUE, Qt::UserRole, (bool)r.ask);
		it->setData(CW_VALUE, Qt::UserRole + 1, r.val);
		if (r.ask) it->setText(CW_VALUE, (r.val < 0) ? QString("?") : QString::number(r.val));
		QString mark;
		QColor col;
		if (r.why) {
			mark = QString("bad: %0, line %1").arg(cw_bad[r.why]).arg(r.line);
			col = grey;
		} else {
			it->setData(CW_CHECK, Qt::CheckStateRole, r.on ? Qt::Checked : Qt::Unchecked);
			switch (r.mark) {
				case POK_MISMATCH: mark = "does not match this release"; col = warn; break;
				case POK_UNCHECKED: mark = "cannot check"; col = grey; break;
				case POK_NO_PAGE: mark = "needs 128K"; col = grey; break;
				case POK_NO_RAM: mark = "no RAM there"; col = grey; break;
			}
		}
		if (!mark.isEmpty()) {
			it->setText(CW_NOTE, mark);
			it->setForeground(CW_NOTE, col);
		}
		if (r.mark == POK_UNCHECKED)
			it->setToolTip(CW_NOTE, "The file has no original bytes for it to compare");
		if (r.why || (r.mark == POK_NO_PAGE) || (r.mark == POK_NO_RAM))
			it->setForeground(CW_NAME, grey);
		foreach(const xPoke& pk, r.pokes) {
			QTreeWidgetItem* sub = new QTreeWidgetItem(it);
			sub->setText(CW_NAME, cw_poke_text(pk));
			sub->setForeground(CW_NAME, grey);
		}
		tree->addTopLevelItem(it);
		if (open.contains(idx)) it->setExpanded(true);
	}
	tree->resizeColumnToContents(CW_NOTE);
	tree->setColumnWidth(CW_NOTE, tree->columnWidth(CW_NOTE) + 8);	// the text off the frame
	tree->verticalScrollBar()->setValue(scroll);
	bool ask = false;
	foreach(const xCheatRow& r, rows) ask |= (r.ask != 0);
	tree->setColumnHidden(CW_VALUE, !ask);
	rzx = conf.zx->rzx.play;
	if (rzx) note("An RZX is playing: nothing goes in while it plays.");
}

void xCheatWin::upd() {
	if (!isVisible()) return;
	if ((serial != cheats_serial()) || (rzx != (bool)conf.zx->rzx.play)) {
		if (!conf.zx->rzx.play && rzx) note(QString());
		fill();
	}
}

void xCheatWin::showWindow() {
	if ((conf.dbg.cheatpos.x() >= 0) && (conf.dbg.cheatpos.y() >= 0)) {
		setGeometry(QRect(conf.dbg.cheatpos, conf.dbg.cheatsiz));
	} else {
		resize(conf.dbg.cheatsiz);
		center_over(this, parentWidget());
	}
	show();
	raise();
	activateWindow();
}

void xCheatWin::showEvent(QShowEvent* ev) {
	QDialog::showEvent(ev);
	fill();
}

// where the window is left is where it comes back
void xCheatWin::moveEvent(QMoveEvent* ev) {
	QDialog::moveEvent(ev);
	if (!isVisible()) return;
	conf.dbg.cheatpos = geometry().topLeft();
}

void xCheatWin::resizeEvent(QResizeEvent* ev) {
	QDialog::resizeEvent(ev);
	if (!isVisible()) return;
	conf.dbg.cheatsiz = geometry().size();
}
