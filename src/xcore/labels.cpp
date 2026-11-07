#include "xcore.h"
#include "arcpath.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileDialog>

#include <QDebug>

// labels

xLabelSet* createLabelSet(QString name) {
	xLabelSet* set = new xLabelSet;
	set->name = name;
	set->list.clear();
	conf.labsets.append(set);
	return set;
}

// return pointer to existing labelset for given name, or nullptr if doesn't exist
xLabelSet* findLabelSet(QString name) {
	xLabelSet* ptr = nullptr;
	xLabelSet* res = nullptr;
	foreach (ptr, conf.labsets) {
		if (ptr->name == name) {
			res = ptr;
		}
	}
	return res;
}

// bumped on every change to the labels in effect: what a compiled expression
// remembers of a label is good while this stays the same
static unsigned labGen = 1;

unsigned labels_gen() {
	return labGen;
}

void labels_touch() {
	labGen++;
}

// ROM labels: res/labels/*.labels name the routines of known ROMs. A list is
// for the 16K banks whose MD5 its "@rom" lines give; a patched bank leaves out
// the names whose code changed. They are found by the bytes, so a ROM file of
// the user's with the same bytes has them too.

struct romLabList {
	QList<QPair<int, QString> > labs;		// offset in the bank, name
	QHash<QByteArray, QSet<QString> > banks;	// MD5 (hex) of a bank -> names it has not
};

static QList<romLabList> romLists;
static QHash<int, QString> romByAbs;		// ROM cell -> name, for the machine in use
static QMap<QString, QList<int> > romByName;	// name -> its cells, lowest first

static void rom_lists_read() {
	static bool done = false;
	if (done) return;
	done = true;
	QDir dir(":/res/labels");
	foreach(QString fn, dir.entryList(QStringList("*.labels"), QDir::Files, QDir::Name)) {
		QFile file(dir.filePath(fn));
		if (!file.open(QFile::ReadOnly)) continue;
		romLabList lst;
		while (!file.atEnd()) {
			QString line = QString::fromLatin1(file.readLine()).trimmed();
			if (line.isEmpty() || line.startsWith(';')) continue;
			QStringList arr = line.split(' ', X_SkipEmptyParts);
			if (arr.first() == "@rom") {
				if (arr.size() < 2) continue;
				QSet<QString> drop;
				foreach(QString wrd, arr.mid(2))
					if (wrd.startsWith('-')) drop.insert(wrd.mid(1));
				lst.banks[arr.at(1).toLatin1().toLower()] = drop;
			} else if (arr.size() == 2) {
				bool ok;
				int ofs = arr.at(0).toInt(&ok, 16);
				if (ok && (ofs >= 0) && (ofs < MEM_16K)) lst.labs.append(qMakePair(ofs, arr.at(1)));
			}
		}
		romLists.append(lst);
	}
}

// The machine's ROM, as loaded: called under emu_lock whenever it may have changed
void labels_rom_scan(Computer* comp) {
	romByAbs.clear();
	romByName.clear();
	labGen++;
	if (!comp) return;
	rom_lists_read();
	int size = comp->mem->romMask + 1;
	for (int bank = 0; bank + MEM_16K <= size; bank += MEM_16K) {
		QByteArray md5 = QCryptographicHash::hash(QByteArray::fromRawData((const char*)comp->mem->romData + bank, MEM_16K),
			QCryptographicHash::Md5).toHex();
		foreach(const romLabList& lst, romLists) {
			QHash<QByteArray, QSet<QString> >::const_iterator it = lst.banks.constFind(md5);
			if (it == lst.banks.constEnd()) continue;
			for (int i = 0; i < lst.labs.size(); i++) {
				const QString& nm = lst.labs.at(i).second;
				if (it->contains(nm)) continue;
				int abs = bank + lst.labs.at(i).first;
				romByAbs[abs] = nm;
				romByName[nm].append(abs);
			}
		}
	}
	if (!romByAbs.isEmpty()) xlog(XLG_FILE, XLL_INFO, "ROM labels: %i", romByAbs.size());
}

QStringList labels_rom_names() {
	return conf.dbg.romlabels ? romByName.keys() : QStringList();
}

// the cell of a ROM label: the one paged in now if the name is in more banks
xAdr labels_rom_cell(const QString& nm) {
	xAdr xadr = {-1, -1, -1, -1};
	if (!conf.dbg.romlabels || !romByName.contains(nm)) return xadr;
	const QList<int>& cells = romByName[nm];
	xadr.type = MEM_ROM;
	xadr.abs = cells.first();
	if (conf.zx) {
		foreach(int abs, cells) {
			if (comp_find_xadr(conf.zx, MEM_ROM, abs, MEM_VIEW_READ) >= 0) {
				xadr.abs = abs;
				break;
			}
		}
	}
	xadr.bank = xadr.abs >> 14;
	xadr.adr = conf.zx ? comp_find_xadr(conf.zx, MEM_ROM, xadr.abs, MEM_VIEW_READ) : -1;
	return xadr;
}

// create conf.labmap from current labset
void map_labels() {
	xLabelSet* set = conf.curlabset;
	labGen++;
	conf.labmap.clear();
	if (set) {
		xAdr xadr;
		QString nm;
		foreach(nm, set->list.keys()) {
			xadr = set->list[nm];
			conf.labmap[xadr.type][xadr.abs] = nm;
		}
	}
}

int delLabelSet(QString name) {
	int res = 0;
	xLabelSet* cur = conf.curlabset;
	xLabelSet* ptr;
	for (int i = conf.labsets.size() - 1; i >= 0; i--) {
		ptr = conf.labsets.at(i);
		if (ptr->name == name) {
			conf.labsets.removeAt(i);
			if (cur == ptr) {		// delete current?
				conf.curlabset = conf.labsets.size() ? conf.labsets.at(0) : nullptr;
			}
			delete(ptr);
			res++;
		}
	}
	map_labels();
	return res;
}

// returns pointer to existing labels set for given name, or add new labelset and return its pointer
xLabelSet* newLabelSet(QString name) {
	xLabelSet* res = findLabelSet(name);
	if (!res) {
		res = createLabelSet(name);
	}
	return res;
}

void setLabelSet(xLabelSet* set) {
	conf.curlabset = set;
	map_labels();			// recreate labmap
}

xLabelSet* setLabelSet(QString path) {
	xLabelSet* set = findLabelSet(path);
	if (set) {
		setLabelSet(set);
	}
	return set;
}

void clear_labels() {
	xLabelSet* set = conf.curlabset;
	if (set) {
		set->list.clear();
		conf.labmap.clear();
	}
	labGen++;
}

void clear_all_labels() {
	while(!conf.labsets.isEmpty()) {
		free(conf.labsets.first());
		conf.labsets.takeFirst();
	}
	labGen++;
}

void del_label(QString name) {
	xLabelSet* set = conf.curlabset;
	if (set) {
		if (set->list.contains(name)) {
			xAdr xadr = set->list[name];
			set->list.remove(name);
			conf.labmap[xadr.type].remove(xadr.abs);
		}
	}
	labGen++;
}

void add_label(xAdr xadr, QString name, xLabelSet* set) {
	if (!set) {
		set = conf.curlabset;
	}
	if (!set) {				// if no current labset
		set = newLabelSet("noname");	// new internal labset
		setLabelSet(set);
	}
	if (set) {
		if (set->list.contains(name))
			del_label(name);
		set->list[name] = xadr;
		conf.labmap[xadr.type][xadr.abs] = name;
	}
	labGen++;
}

QString find_label(xAdr xadr) {
	QString lab;
	xLabelSet* set = conf.curlabset;
	if (set) {
		if (conf.labmap.contains(xadr.type)) {
			if (conf.labmap[xadr.type].contains(xadr.abs)) {
				lab = conf.labmap[xadr.type][xadr.abs];
			}
		}
		// a label at a CPU address names whatever cell is there: a page view passes
		// an offset in its adr, and the cell at that offset is not that CPU address
		if (lab.isEmpty() && (xadr.adr >= 0) && conf.labmap.contains(MEM_IO) && conf.labmap[MEM_IO].contains(xadr.adr)) {
			xAdr now = conf.zx ? comp_get_xadr(conf.zx, xadr.adr, MEM_VIEW_READ) : xadr;
			if ((now.type == xadr.type) && (now.abs == xadr.abs)) lab = conf.labmap[MEM_IO].value(xadr.adr);
		}
	}
	// the user's labels first, then the ROM's
	if (lab.isEmpty() && (xadr.type == MEM_ROM) && conf.dbg.romlabels)
		lab = romByAbs.value(xadr.abs);
	return lab;
}

// the cell a label names, its CPU address as it was when the label was read
xAdr find_label_cell(QString nm) {
	xAdr xadr = {-1, -1, -1, -1};
	xLabelSet* set = conf.curlabset;
	if (set) xadr = set->list.value(nm, xadr);
	if (xadr.type < 0) xadr = labels_rom_cell(nm);
	return xadr;
}

// that cell's CPU address now: on the Next RAM, and ROM everywhere, follow the
// paging, -1 when the cell is not paged in
xAdr label_cpu(xAdr xadr) {
	if (conf.zx && (((conf.zx->hw->id == HW_ZXNEXT) && (xadr.type == MEM_RAM)) || (xadr.type == MEM_ROM)))
		xadr.adr = comp_find_xadr(conf.zx, xadr.type, xadr.abs, MEM_VIEW_READ);
	return xadr;
}

xAdr find_label(QString nm) {
	return label_cpu(find_label_cell(nm));
}

// The page of a LABELSLIST line. SjASMPlus writes it in decimal; a page with a
// hex digit in it comes from Xpeccy+ before 2026.8.2, which wrote hex. FF and an
// empty page (":ADDR") are a CPU address: -2. -1: not a page.
static int labels_page(const QString& str) {
	if (str.isEmpty() || (str.compare("FF", Qt::CaseInsensitive) == 0)) return -2;
	bool ok;
	int page = str.toInt(&ok, 10);
	if (!ok) page = str.toInt(&ok, 16);
	return (ok && (page >= 0)) ? page : -1;
}

// ...and back, the way SjASMPlus writes it
static QString labels_page_str(int page) {
	return QString("%1").arg(page, 2, 10, QChar('0'));
}

// A label at RAM page:offset, in the pages the machine in use has: the Next's
// 8K pages (in its SRAM image, after the ROM and divMMC areas), 16K banks elsewhere
static void labels_add_ram(bool isNext, int page, int ofs, const QString& name) {
	xAdr xadr;
	xadr.type = MEM_RAM;
	if (isNext) {
		if ((page < 0) || (page >= 0xe0)) return;
		xadr.abs = 0x40000 + (page << 13) + (ofs & 0x1fff);
		xadr.bank = xadr.abs >> 14;
		xadr.adr = comp_find_xadr(conf.zx, xadr.type, xadr.abs, MEM_VIEW_READ);
	} else {
		xadr.bank = page;
		xadr.abs = (page << 14) | (ofs & 0x3fff);
		switch (page) {		// where a 128K sees it
			case 0x05: xadr.adr = 0x4000 | (ofs & 0x3fff); break;
			case 0x02: xadr.adr = 0x8000 | (ofs & 0x3fff); break;
			default: xadr.adr = 0xc000 | (ofs & 0x3fff); break;
		}
	}
	add_label(xadr, name);
}

// A label at a CPU address: the Next keeps it there whatever is paged in, other
// machines take it into the 128K's bank behind it
static void labels_add_cpu(bool isNext, int adr, const QString& name) {
	if (isNext) {
		xAdr xadr;
		xadr.type = MEM_IO;
		xadr.abs = adr;
		xadr.adr = adr;
		xadr.bank = -1;
		add_label(xadr, name);
		return;
	}
	static const int bank[4] = {0, 5, 2, 0};
	labels_add_ram(false, bank[(adr >> 14) & 3], adr, name);
}

static bool labels_hex(const QString& str, int len, int* val) {
	bool ok;
	*val = str.toInt(&ok, 16);
	return ok && (str.size() == len);
}

// A SjASMPlus CSPECTMAP line: "logical linear type name", the linear address over
// the device's RAM - page * page size + offset, so it needs no page size to place.
// Only labels (type 00) and ROM / no-device ones (03, a CPU address) are kept;
// EQU, DEFL and STRUCT values are not places. false: not a map line.
static bool labels_map_line(bool isNext, const QString& line) {
	QStringList arr = line.simplified().split(' ');
	int logical, linear, type;
	if ((arr.size() < 4) || !labels_hex(arr.at(0), 8, &logical) || !labels_hex(arr.at(1), 8, &linear)
			|| !labels_hex(arr.at(2), 2, &type))
		return false;
	if (type == 0) {
		if (isNext) labels_add_ram(true, linear >> 13, linear & 0x1fff, arr.at(3));
		else labels_add_ram(false, linear >> 14, linear & 0x3fff, arr.at(3));
	} else if (type == 3) {
		labels_add_cpu(isNext, logical & 0xffff, arr.at(3));
	}
	return true;
}

// A line of Xpeccy+'s own for memory that is not the machine's RAM pages:
// ROM0:0123, DIV3:0010 (saveLabels). false: not one
static bool labels_area_line(const QStringList& arr) {
	int area, page, type, abs;
	bool ok;
	if (!conf.zx || !cell_token_parse(arr.at(0).toLatin1().constData(), &area, &page)) return false;
	int ofs = arr.at(1).toInt(&ok, 16);
	if (!ok || !comp_cell_xadr(conf.zx, area, page, ofs, &type, &abs)) return true;
	xAdr xadr;
	xadr.type = type;
	xadr.abs = abs;
	xadr.bank = abs >> 14;
	xadr.adr = comp_find_xadr(conf.zx, type, abs, MEM_VIEW_READ);
	add_label(xadr, arr.at(2));
	return true;
}

// One SjASMPlus LABELSLIST or CSPECTMAP file into the set named after it, which
// is then the one in effect. Its pages are taken as the machine in use has them.
// 0: it could not be opened, and nothing changed.
static int labels_read(const QString& path) {
	QString line;
	QStringList arr;
	QFile file(path);
	if (!file.open(QFile::ReadOnly)) return 0;
	bool isNext = conf.zx && (conf.zx->hw->id == HW_ZXNEXT);
	xLabelSet* set = newLabelSet(QFileInfo(path).fileName());
	set->list.clear();		// before it is made current, or its old map is built for nothing
	setLabelSet(set);
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
	static const QRegularExpression sep("[: \t\r\n]");
#else
	static const QRegExp sep("[: \t\r\n]");
#endif
	int wide = 0;		// offsets past 8K: a 16K-page file
	while(!file.atEnd()) {
		line = file.readLine();
		if (labels_map_line(isNext, line)) continue;
		if (line.startsWith(":"))
			line.prepend("FF");
		arr = line.split(sep, X_SkipEmptyParts);
		if ((arr.size() > 2) && labels_area_line(arr)) continue;
		if (arr.size() > 2) {
			bool adrOk;
			int page = labels_page(arr.at(0));
			int adr = arr.at(1).toInt(&adrOk, 16);
			if ((page == -1) || !adrOk || (adr < 0) || (adr > 0xffff)) continue;
			if (page == -2) {
				labels_add_cpu(isNext, adr, arr.at(2));
			} else {
				if (adr > 0x1fff) wide++;
				labels_add_ram(isNext, page, adr, arr.at(2));
			}
		}
	}
	xlog(XLG_FILE, XLL_INFO, "labels from %s", path.toLocal8Bit().constData());
	if (isNext && wide)
		xlog(XLG_FILE, XLL_WARN, "labels: %i offsets past 8K, the file has 16K pages - read as the Next's 8K ones", wide);
	return 1;
}

// A file the user named, on the command line or in the debugger: the one
// Reload reads again. NULL asks for it.
int loadLabels(const char* fn) {
	QString path(fn);
	if (path.isEmpty())
		path = QFileDialog::getOpenFileName(NULL, "Load SJASM labels",QString(),QString(),nullptr,QFileDialog::DontUseNativeDialog);
	if (path.isEmpty()) return 1;
	if (!labels_read(path)) return 0;
	conf.labpath = path;
	return 1;
}

bool labels_named_on() {
	return !conf.labpath.isEmpty() && conf.curlabset
		&& (conf.curlabset->name == QFileInfo(conf.labpath).fileName());
}

// the labels an image came with last, and the set in effect before them
static QString imgSet;
static QString imgBefore;

void labels_image(const QString& image) {
	if (!imgSet.isEmpty()) {
		// a set picked by hand since then stays
		if (conf.curlabset && (conf.curlabset->name == imgSet))
			setLabelSet(findLabelSet(imgBefore));
		imgSet.clear();
	}
	if (!conf.dbg.imglabels || image.isEmpty()) return;
	QString path = arc_beside(image, ".labels");		// an archive's: beside the archive
	if (!QFileInfo(path).isFile()) return;
	QString before = conf.curlabset ? conf.curlabset->name : QString();
	if (!labels_read(path)) return;
	imgBefore = before;
	imgSet = conf.curlabset->name;
}

// One label as a LABELSLIST line: RAM in the machine's pages, the page in decimal
// as SjASMPlus writes it; any other memory as its area's token (ROM0:0123,
// DIV3:0010), which only Xpeccy+ reads back; a CPU address as :ADDR. Empty: a
// cell with no name (the SRAM behind the Next's banks 5 and 7)
static QString labels_line(const QString& name, xAdr xadr) {
	if (xadr.type == MEM_IO) return QString(":%0 %1\n").arg(gethexword(xadr.abs), name);
	if (!conf.zx) return QString();
	xCell cell = comp_cell(conf.zx, xadr.type, xadr.abs);
	if (cell.area == CELL_RAM)
		return QString("%0:%1 %2\n").arg(labels_page_str(cell.page), gethexword(cell.ofs), name);
	if (cell.area == CELL_NONE) return QString();
	char buf[16];
	return QString("%0:%1 %2\n").arg(cell_token(cell, buf), gethexword(cell.ofs), name);
}

int saveLabels(const char* fn) {
	int res = 1;
	QStringList keys;
	QString key;
	QString line;
	QFile file;
	QString path(fn);
	if (path.isEmpty())
		path = QFileDialog::getSaveFileName(NULL, "Save labels",QString(),QString(),nullptr,QFileDialog::DontUseNativeDialog);
	if (path.isEmpty()) {
		res = 0;
	} else {
		file.setFileName(path);
		if (file.open(QFile::WriteOnly)) {
			xLabelSet* set = conf.curlabset;
			if (set) {
				keys = set->list.keys();
				int lost = 0;
				foreach(key, keys) {
					line = labels_line(key, set->list.value(key));
					if (line.isEmpty()) {
						lost++;
					} else {
						file.write(line.toUtf8());
					}
				}
				if (lost) xlog(XLG_FILE, XLL_WARN, "labels: %i not saved, in memory with no name to write", lost);
			}
			file.close();
		} else {
			res = 0;
		}
	}
	return res;
}

// comments

void add_comment(xAdr xadr, QString str) {
	conf.commap[xadr.type][xadr.abs] = str;
}

void del_comment(xAdr xadr) {
	if (conf.commap.contains(xadr.type)) {
		conf.commap[xadr.type].remove(xadr.abs);
	}
}

QString find_comment(xAdr xadr) {
	QString str;
	if (conf.commap.contains(xadr.type)) {
		if (conf.commap[xadr.type].contains(xadr.abs)) {
			str = conf.commap[xadr.type][xadr.abs];
		}
	}
	return str;
}

void clear_comments() {
	conf.commap.clear();
}
