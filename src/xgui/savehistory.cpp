#include "savehistory.h"
#include "xcore/xcore.h"
#include "xcore/saves.h"
#include "libxpeccy/filetypes/filetypes.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

static QString mb_text(qint64 bytes) {
	return QString::number(bytes / 1048576.0, 'f', 1);
}

static QString machine_of(const QString& path) {
	char id[256] = "";
	szx_machine_of(path.toLocal8Bit().constData(), id, sizeof(id));
	const xMachine* m = *id ? xm_find(id) : nullptr;
	return m ? QString::fromLocal8Bit(m->name.c_str()) : QString::fromLocal8Bit(id);
}

// A table whose column stretch takes the room left, at least chars characters
// or its widest text (longer text cut short); each other column fixed, as wide as its heading or
// the widest text it can hold (widest: the candidates one a line), so what is
// in the rows never moves the window. The figures from column num on
// right-aligned under their headings. The widths are kept with the table and
// put on by sh_apply(): showing the window puts the columns back to the default.
static QTreeWidget* sh_table(const QStringList& heads, const QStringList& widest, int stretch, int chars, int num) {
	QTreeWidget* t = new QTreeWidget;
	t->setRootIsDecorated(false);
	t->setUniformRowHeights(true);
	t->setAllColumnsShowFocus(true);
	t->setHeaderLabels(heads);
	t->setContextMenuPolicy(Qt::CustomContextMenu);
	QHeaderView* hv = t->header();
	hv->setStretchLastSection(false);
	hv->setSectionsMovable(false);
	hv->setMinimumSectionSize(1);
	int margin = 2 * t->style()->pixelMetric(QStyle::PM_HeaderMargin, nullptr, hv) + 8;
	QVariantList widths;
	for (int i = 0; i < heads.size(); i++) {
		if (i >= num) t->headerItem()->setTextAlignment(i, Qt::AlignRight | Qt::AlignVCenter);
		int w = hv->fontMetrics().horizontalAdvance(heads.at(i));
		for (const QString& s : widest.value(i).split('\n'))
			w = qMax(w, t->fontMetrics().horizontalAdvance(s));
		widths.append((i == stretch) ? 0 : w + margin);
		hv->setSectionResizeMode(i, (i == stretch) ? QHeaderView::Stretch : QHeaderView::Fixed);
		if (i == stretch) t->setProperty("shMin", qMax(t->fontMetrics().averageCharWidth() * chars, w + margin));
	}
	t->setProperty("shWidths", widths);
	return t;
}

// columns of one kind as wide as the widest of them
static void sh_same(QTreeWidget* t, const QList<int>& cols) {
	QVariantList widths = t->property("shWidths").toList();
	int w = 0;
	for (int i : cols) w = qMax(w, widths.at(i).toInt());
	for (int i : cols) widths[i] = w;
	t->setProperty("shWidths", widths);
}

// the fixed columns at their widths, the table no narrower than they and the
// stretching one's least
static void sh_apply(QTreeWidget* t) {
	QVariantList widths = t->property("shWidths").toList();
	int wid = t->property("shMin").toInt();
	for (int i = 0; i < widths.size(); i++) {
		int w = widths.at(i).toInt();
		if (w <= 0) continue;
		t->header()->resizeSection(i, w);
		wid += w;
	}
	t->setMinimumWidth(wid + 2 * t->frameWidth() + t->style()->pixelMetric(QStyle::PM_ScrollBarExtent));
}

// rows high in view
static void sh_rows(QTreeWidget* t, int rows) {
	int row = t->fontMetrics().height() + 6;
	t->setFixedHeight(t->header()->sizeHint().height() + rows * row + 2 * t->frameWidth());
}

// the ways a time of a save is written, one a line, digits at their widest
static QString sh_times() {
	QDateTime now = QDateTime::currentDateTime();
	QString res = QStringList({saves_time_text(now), saves_time_text(now.addDays(-1)), saves_time_text(now.addDays(-7))}).join('\n');
	return res.replace(QRegularExpression("[0-9]"), "8");
}

static void sh_right(QTreeWidgetItem* it, int from) {
	for (int i = from; i < it->columnCount(); i++)
		it->setTextAlignment(i, Qt::AlignRight | Qt::AlignVCenter);
}

QString save_history(QWidget* parent, const QString& current) {
	QDialog dia(parent);
	dia.setWindowTitle("Save History");
	QVBoxLayout* lay = new QVBoxLayout(&dia);

	QTreeWidget* games = sh_table({"Game", "Last save", "Quick", "Auto", "MB", "Old", "Old MB"},
		{QString(), sh_times(), "888", "888", "8888.8", "888", "8888.8"}, 0, 26, 2);
	sh_same(games, {2, 3, 5});
	sh_same(games, {4, 6});
	sh_rows(games, 10);
	games->setObjectName("shGames");
	games->headerItem()->setToolTip(5, "Saves Clear Old deletes");
	games->headerItem()->setToolTip(6, "What Clear Old frees");
	lay->addWidget(games);

	QHBoxLayout* row = new QHBoxLayout;
	QLabel* labDays = new QLabel("Older than");
	QSpinBox* sbDays = new QSpinBox;
	sbDays->setRange(1, 999);
	sbDays->setSuffix(" days");
	sbDays->setValue(conf.saves.cleanDays);
	labDays->setBuddy(sbDays);
	QCheckBox* cbKeep = new QCheckBox("Keep at least one save");
	cbKeep->setToolTip("Each game keeps its newest save, however old");
	cbKeep->setChecked(conf.saves.keepOne);
	QPushButton* pbClear = new QPushButton(QIcon(":/images/cancel.png"), "Clear Old");
	pbClear->setToolTip("Delete the saves older than this, in every game");
	QPushButton* pbDelGame = new QPushButton(QIcon(":/images/cancel.png"), "Delete Game");
	pbDelGame->setToolTip("Delete every save of the game picked above");
	row->addWidget(labDays);
	row->addWidget(sbDays);
	row->addSpacing(12);
	row->addWidget(cbKeep);
	row->addStretch(1);
	row->addWidget(pbDelGame);
	row->addWidget(pbClear);
	lay->addLayout(row);

	QHBoxLayout* low = new QHBoxLayout;
	QStringList types, macs;
	for (int k = 0; k < SAVE_KINDS; k++) types.append(saves_kind_name(k));
	for (const xMachine& m : xm_list()) macs.append(QString::fromLocal8Bit(m.name.c_str()));
	QTreeWidget* saves = sh_table({"Saved", "Type", "Machine", "KB"}, {sh_times(), types.join('\n'), macs.join('\n'), "88888"}, 2, 12, 3);
	saves->setObjectName("shSaves");
	saves->setSelectionMode(QAbstractItemView::ExtendedSelection);
	low->addWidget(saves, 1);
	QVBoxLayout* side = new QVBoxLayout;
	QLabel* pic = new QLabel;
	pic->setFixedSize(260, 196);		// a Spectrum's screen 1:1
	pic->setAlignment(Qt::AlignCenter);
	pic->setFrameShape(QFrame::StyledPanel);
	side->addWidget(pic);
	QPushButton* pbLoad = new QPushButton(QIcon(":/images/fileopen.png"), "Load");
	pbLoad->setDefault(true);
	QPushButton* pbDel = new QPushButton(QIcon(":/images/cancel.png"), "Delete");
	QPushButton* pbShow = new QPushButton("Show in Folder");
	side->addWidget(pbLoad);
	side->addWidget(pbDel);
	side->addWidget(pbShow);
	side->addStretch(1);
	low->addLayout(side);
	lay->addLayout(low, 1);

	QHBoxLayout* bottom = new QHBoxLayout;
	bottom->addStretch(1);
	QPushButton* pbClose = new QPushButton("Close");
	bottom->addWidget(pbClose);
	lay->addLayout(bottom);

	QList<xSaveFolder> folders;
	QString chosen;

	// the old ones, as the two settings above say
	auto fillOld = [&]() {
		for (int i = 0; i < games->topLevelItemCount() && i < folders.size(); i++) {
			QList<xSave> old = saves_old(folders.at(i), sbDays->value(), cbKeep->isChecked());
			qint64 size = 0;
			for (const xSave& sv : old) size += sv.size;
			QTreeWidgetItem* it = games->topLevelItem(i);
			it->setText(5, old.isEmpty() ? QString("-") : QString::number(old.size()));
			it->setText(6, old.isEmpty() ? QString("-") : mb_text(size));
		}
	};
	auto fillSaves = [&]() {
		saves->clear();
		pic->clear();
		int i = games->indexOfTopLevelItem(games->currentItem());
		if ((i < 0) || (i >= folders.size())) return;
		for (const xSave& sv : folders.at(i).saves) {
			QString mac = machine_of(sv.path);
			QTreeWidgetItem* it = new QTreeWidgetItem({saves_time_text(sv.time), saves_kind_name(sv.kind),
				mac, QString::number((sv.size + 1023) / 1024)});
			it->setData(0, Qt::UserRole, sv.path);
			it->setData(1, Qt::UserRole, sv.pic);
			it->setToolTip(2, mac);		// cut short when the window is narrow
			sh_right(it, 3);
			saves->addTopLevelItem(it);
		}
		if (saves->topLevelItemCount()) saves->setCurrentItem(saves->topLevelItem(0));
		sh_apply(saves);
	};
	auto fillGames = [&](const QString& keep) {
		folders = saves_folders();
		games->clear();
		QTreeWidgetItem* cur = nullptr;
		for (const xSaveFolder& f : folders) {
			int quick = 0;
			for (const xSave& sv : f.saves) quick += (sv.kind == SAVE_QUICK) ? 1 : 0;
			QString name = f.machine ? f.name + " (no game)" : f.name;
			QTreeWidgetItem* it = new QTreeWidgetItem({name, saves_time_text(f.saves.first().time),
				QString::number(quick), QString::number(f.saves.size() - quick), mb_text(f.size), QString(), QString()});
			it->setToolTip(0, name + "\n" + QDir::toNativeSeparators(f.images.isEmpty() ? f.dir : f.images.first()));
			sh_right(it, 2);
			games->addTopLevelItem(it);
			if (f.dir == keep) cur = it;
		}
		fillOld();
		sh_apply(games);
		games->setCurrentItem(cur ? cur : games->topLevelItem(0));
		fillSaves();
	};
	auto selected = [&]() {
		QList<xSave> res;
		int i = games->indexOfTopLevelItem(games->currentItem());
		if ((i < 0) || (i >= folders.size())) return res;
		for (QTreeWidgetItem* it : saves->selectedItems()) {
			int j = saves->indexOfTopLevelItem(it);
			if ((j >= 0) && (j < folders.at(i).saves.size())) res.append(folders.at(i).saves.at(j));
		}
		return res;
	};
	auto curDir = [&]() {
		int i = games->indexOfTopLevelItem(games->currentItem());
		return ((i >= 0) && (i < folders.size())) ? folders.at(i).dir : QString();
	};

	QObject::connect(games, &QTreeWidget::currentItemChanged, &dia, [&]() {fillSaves();});
	QObject::connect(saves, &QTreeWidget::currentItemChanged, &dia, [&](QTreeWidgetItem* it) {
		QString file = it ? it->data(1, Qt::UserRole).toString() : QString();
		QPixmap pm(file);
		pic->setPixmap(pm.isNull() ? QPixmap() : pm.scaled(pic->size() - QSize(4, 4), Qt::KeepAspectRatio, Qt::SmoothTransformation));
	});
	QObject::connect(sbDays, QOverload<int>::of(&QSpinBox::valueChanged), &dia, [&]() {fillOld();});
	QObject::connect(cbKeep, &QCheckBox::toggled, &dia, [&]() {fillOld();});
	QObject::connect(pbClear, &QPushButton::clicked, &dia, [&]() {
		QList<xSave> old;
		qint64 size = 0;
		for (const xSaveFolder& f : folders) {
			for (const xSave& sv : saves_old(f, sbDays->value(), cbKeep->isChecked())) {
				old.append(sv);
				size += sv.size;
			}
		}
		if (old.isEmpty()) {
			QMessageBox::information(&dia, "Clear Old", "No save is that old.");
			return;
		}
		QString q = QString("Delete %0 saves older than %1 days (%2 MB)?").arg(old.size()).arg(sbDays->value()).arg(mb_text(size));
		if (QMessageBox::question(&dia, "Clear Old", q, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
		QString keep = curDir();
		saves_delete(old);
		for (const xSaveFolder& f : folders) saves_drop_empty(f.dir);
		fillGames(keep);
	});
	auto delSaves = [&]() {
		QList<xSave> list = selected();
		if (list.isEmpty()) return;
		QString q = (list.size() == 1) ? QString("Delete the save of %0?").arg(saves_time_text(list.first().time))
			: QString("Delete %0 saves?").arg(list.size());
		if (QMessageBox::question(&dia, "Delete", q, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
		QString keep = curDir();
		saves_delete(list);
		saves_drop_empty(keep);
		fillGames(keep);
	};
	// every save of the game picked, its folder with them; the next game picked then
	auto delGame = [&]() {
		int i = games->indexOfTopLevelItem(games->currentItem());
		if ((i < 0) || (i >= folders.size())) return;
		const xSaveFolder f = folders.at(i);
		QString q = QString("Delete all %0 saves of %1 (%2 MB)?").arg(f.saves.size())
			.arg(games->currentItem()->text(0), mb_text(f.size));
		if (QMessageBox::question(&dia, "Delete Game", q, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
		QString next = (i + 1 < folders.size()) ? folders.at(i + 1).dir : (i > 0) ? folders.at(i - 1).dir : QString();
		saves_delete(f.saves);
		saves_drop_empty(f.dir);
		fillGames(next);
	};
	auto load = [&]() {
		QList<xSave> list = selected();
		if (list.size() != 1) return;
		chosen = list.first().path;
		dia.accept();
	};
	auto show = [&]() {
		QString dir = curDir();
		QDesktopServices::openUrl(QUrl::fromLocalFile(dir.isEmpty() ? saves_root() : dir));
	};
	QObject::connect(pbDel, &QPushButton::clicked, &dia, delSaves);
	QObject::connect(pbDelGame, &QPushButton::clicked, &dia, delGame);
	QObject::connect(pbLoad, &QPushButton::clicked, &dia, load);
	QObject::connect(saves, &QTreeWidget::itemActivated, &dia, load);	// a double click, Enter
	QObject::connect(pbShow, &QPushButton::clicked, &dia, show);
	// the Delete key and a right click, in each table its own
	auto delKey = [](QWidget* w) {
		QShortcut* sc = new QShortcut(QKeySequence(QKeySequence::Delete), w);
		sc->setContext(Qt::WidgetShortcut);
		return sc;
	};
	QObject::connect(delKey(games), &QShortcut::activated, &dia, delGame);
	QObject::connect(delKey(saves), &QShortcut::activated, &dia, delSaves);
	QObject::connect(games, &QWidget::customContextMenuRequested, &dia, [&](const QPoint& pos) {
		if (!games->itemAt(pos)) return;
		QMenu menu(&dia);
		menu.addAction(QIcon(":/images/cancel.png"), "Delete Game", delGame);
		menu.addAction("Show in Folder", show);
		menu.exec(games->viewport()->mapToGlobal(pos));
	});
	QObject::connect(saves, &QWidget::customContextMenuRequested, &dia, [&](const QPoint& pos) {
		if (!saves->itemAt(pos)) return;
		QMenu menu(&dia);
		QAction* act = menu.addAction(QIcon(":/images/fileopen.png"), "Load", load);
		act->setEnabled(selected().size() == 1);
		menu.addAction(QIcon(":/images/cancel.png"), "Delete", delSaves);
		menu.addAction("Show in Folder", show);
		menu.exec(saves->viewport()->mapToGlobal(pos));
	});
	QObject::connect(pbClose, &QPushButton::clicked, &dia, &QDialog::reject);

	fillGames(current);
	saves->setFocus();		// the newest of the game in use: Enter loads it
	dia.adjustSize();
	QTimer::singleShot(0, &dia, [&]() {
		sh_apply(games);
		sh_apply(saves);
	});
	dia.exec();
	// the window's choices are the settings' own
	conf.saves.cleanDays = sbDays->value();
	conf.saves.keepOne = cbKeep->isChecked() ? 1 : 0;
	return chosen;
}
