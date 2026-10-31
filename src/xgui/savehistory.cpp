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
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
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

// a table: the first column takes the room, the figures from column num on
// right-aligned under their headings
static QTreeWidget* sh_table(const QStringList& heads, int num) {
	QTreeWidget* t = new QTreeWidget;
	t->setRootIsDecorated(false);
	t->setUniformRowHeights(true);
	t->setAllColumnsShowFocus(true);
	t->setHeaderLabels(heads);
	t->header()->setStretchLastSection(false);
	t->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	for (int i = 1; i < heads.size(); i++) {
		t->header()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
		if (i >= num) t->headerItem()->setTextAlignment(i, Qt::AlignRight | Qt::AlignVCenter);
	}
	return t;
}

static void sh_right(QTreeWidgetItem* it, int from) {
	for (int i = from; i < it->columnCount(); i++)
		it->setTextAlignment(i, Qt::AlignRight | Qt::AlignVCenter);
}

QString save_history(QWidget* parent) {
	QDialog dia(parent);
	dia.setWindowTitle("Save History");
	dia.resize(720, 600);
	QVBoxLayout* lay = new QVBoxLayout(&dia);

	QTreeWidget* games = sh_table({"Game", "Last save", "Quick", "Auto", "MB", "Old", "Old MB"}, 2);
	games->headerItem()->setToolTip(5, "Saves Clear Old deletes");
	games->headerItem()->setToolTip(6, "What Clear Old frees");
	lay->addWidget(games, 3);

	QHBoxLayout* row = new QHBoxLayout;
	QLabel* labDays = new QLabel("Older than");
	QSpinBox* sbDays = new QSpinBox;
	sbDays->setRange(1, 3650);
	sbDays->setSuffix(" days");
	sbDays->setValue(conf.saves.cleanDays);
	labDays->setBuddy(sbDays);
	QCheckBox* cbKeep = new QCheckBox("Keep at least one save");
	cbKeep->setToolTip("Each game keeps its newest save, however old");
	cbKeep->setChecked(conf.saves.keepOne);
	QPushButton* pbClear = new QPushButton(QIcon(":/images/cancel.png"), "Clear Old");
	pbClear->setToolTip("Delete the saves older than this, in every game");
	row->addWidget(labDays);
	row->addWidget(sbDays);
	row->addSpacing(12);
	row->addWidget(cbKeep);
	row->addStretch(1);
	row->addWidget(pbClear);
	lay->addLayout(row);

	QHBoxLayout* low = new QHBoxLayout;
	QTreeWidget* saves = sh_table({"Saved", "Kind", "Machine", "KB"}, 3);
	saves->setSelectionMode(QAbstractItemView::ExtendedSelection);
	low->addWidget(saves, 1);
	QVBoxLayout* side = new QVBoxLayout;
	QLabel* pic = new QLabel;
	pic->setFixedSize(196, 148);
	pic->setAlignment(Qt::AlignCenter);
	pic->setFrameShape(QFrame::StyledPanel);
	side->addWidget(pic);
	QPushButton* pbLoad = new QPushButton(QIcon(":/images/fileopen.png"), "Load");
	QPushButton* pbDel = new QPushButton(QIcon(":/images/cancel.png"), "Delete");
	QPushButton* pbShow = new QPushButton("Show in Folder");
	side->addWidget(pbLoad);
	side->addWidget(pbDel);
	side->addWidget(pbShow);
	side->addStretch(1);
	low->addLayout(side);
	lay->addLayout(low, 2);

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
			QTreeWidgetItem* it = new QTreeWidgetItem({saves_time_text(sv.time), saves_kind_name(sv.kind),
				machine_of(sv.path), QString::number((sv.size + 1023) / 1024)});
			it->setData(0, Qt::UserRole, sv.path);
			it->setData(1, Qt::UserRole, sv.pic);
			sh_right(it, 3);
			saves->addTopLevelItem(it);
		}
		if (saves->topLevelItemCount()) saves->setCurrentItem(saves->topLevelItem(0));
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
			it->setToolTip(0, f.images.isEmpty() ? QDir::toNativeSeparators(f.dir) : QDir::toNativeSeparators(f.images.first()));
			sh_right(it, 2);
			games->addTopLevelItem(it);
			if (f.dir == keep) cur = it;
		}
		fillOld();
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
	// gone with its picture; the last session's json too, and the config's pointer at it
	auto drop = [&](const QList<xSave>& list) {
		for (const xSave& sv : list) {
			saves_remove(sv);
			QFileInfo fi(sv.path);
			QFile::remove(fi.path() + "/" + fi.completeBaseName() + ".json");
			if (QFileInfo(conf.saves.last) == fi) conf.saves.last.clear();
		}
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
		drop(old);
		for (const xSaveFolder& f : folders) saves_drop_empty(f.dir);
		fillGames(keep);
	});
	QObject::connect(pbDel, &QPushButton::clicked, &dia, [&]() {
		QList<xSave> list = selected();
		if (list.isEmpty()) return;
		QString q = (list.size() == 1) ? QString("Delete the save of %0?").arg(saves_time_text(list.first().time))
			: QString("Delete %0 saves?").arg(list.size());
		if (QMessageBox::question(&dia, "Delete", q, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
		QString keep = curDir();
		drop(list);
		saves_drop_empty(keep);
		fillGames(keep);
	});
	auto load = [&]() {
		QList<xSave> list = selected();
		if (list.size() != 1) return;
		chosen = list.first().path;
		dia.accept();
	};
	QObject::connect(pbLoad, &QPushButton::clicked, &dia, load);
	QObject::connect(saves, &QTreeWidget::itemDoubleClicked, &dia, load);
	QObject::connect(pbShow, &QPushButton::clicked, &dia, [&]() {
		QString dir = curDir();
		QDesktopServices::openUrl(QUrl::fromLocalFile(dir.isEmpty() ? saves_root() : dir));
	});
	QObject::connect(pbClose, &QPushButton::clicked, &dia, &QDialog::reject);

	fillGames(QString());
	dia.exec();
	// the window's choices are the settings' own
	conf.saves.cleanDays = sbDays->value();
	conf.saves.keepOne = cbKeep->isChecked() ? 1 : 0;
	return chosen;
}
