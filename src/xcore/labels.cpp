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

// ROM labels name the routines of known ROMs. A list is for the 16K banks whose
// MD5 its "@rom" lines give; a patched bank leaves out the names whose code
// changed. They are found by the bytes, so a ROM file of the user's with the
// same bytes has them too. A list's names are in its namespace ("@ns BASIC48"):
// BASIC48.LD_BYTES; the short name stands for it where nothing else has it.
// A list may mark ranges as data ("3D00 3FFF db"), shown where the user has
// marked nothing. An "@ram 5" list names RAM cells of that bank, the system
// variables, when a ROM of its namespace is in the machine.

struct romLabData {
	int start;
	int end;
	unsigned char view;	// DBG_VIEW_*
	QString key;		// "@START": how an @rom line drops it
};

struct romLabList {
	QString ns;
	int ram = -1;					// @ram: the bank its names are in
	QList<QPair<int, QString> > labs;		// offset in the bank (CPU address for @ram), full name
	QList<romLabData> data;
	QHash<QByteArray, QSet<QString> > banks;	// MD5 (hex) of a bank -> names and @data it has not
};

static QList<romLabList> romLists;
static QSet<QString> romSpaces;			// every namespace of the lists
static QHash<int, QString> romByAbs;		// ROM cell -> full name, for the machine in use
static QMap<QString, QList<int> > romByName;	// full name -> its cells, lowest first
static QHash<QString, QStringList> romShort;	// short name -> the full names it may stand for
static QByteArray romView;			// ROM cell -> the data mark a list gives it
static QHash<int, QString> sysByAbs;		// RAM cell -> system variable's full name
static QMap<QString, xAdr> sysByName;		// ...and back
static QHash<QString, QStringList> sysShort;
static QList<QByteArray> romBanks;		// the MD5s they were found by
static int romCustomSeen = 0;			// the machine's romCustom at that time
static bool labFull = false;			// names in full, namespace and all

static unsigned char rom_view_code(const QString& str) {
	if (str == "db") return DBG_VIEW_BYTE;
	if (str == "dw") return DBG_VIEW_WORD;
	if (str == "da") return DBG_VIEW_ADDR;
	if (str == "text") return DBG_VIEW_TEXT;
	return DBG_VIEW_CODE;
}

// The lists are files in config/labels of the installed program, each under the
// license of its source (config/labels/README.md)
static void rom_lists_read() {
	static bool done = false;
	if (done) return;
	done = true;
	QStringList paths;
	foreach(QString dname, QStringList() << (xconf_install_dir() + "/labels")) {
		QDir dir(dname);
		if (!dir.exists()) xlog(XLG_FILE, XLL_WARN, "ROM labels: no %s", dname.toLocal8Bit().constData());
		foreach(QString fn, dir.entryList(QStringList("*.labels"), QDir::Files, QDir::Name))
			paths.append(dir.filePath(fn));
	}
	foreach(QString path, paths) {
		QFile file(path);
		if (!file.open(QFile::ReadOnly)) continue;
		romLabList lst;
		while (!file.atEnd()) {
			QString line = QString::fromLatin1(file.readLine()).trimmed();
			if (line.isEmpty() || line.startsWith(';')) continue;
			QStringList arr = line.split(' ', X_SkipEmptyParts);
			if (arr.first() == "@ns") {
				if (arr.size() > 1) lst.ns = arr.at(1);
			} else if (arr.first() == "@ram") {
				if (arr.size() > 1) lst.ram = arr.at(1).toInt();
			} else if (arr.first() == "@rom") {
				if (arr.size() < 2) continue;
				QSet<QString> drop;
				foreach(QString wrd, arr.mid(2))	// a range's address in any case, as its line has it
					if (wrd.startsWith('-')) drop.insert(wrd.startsWith("-@") ? wrd.mid(1).toUpper() : wrd.mid(1));
				lst.banks[arr.at(1).toLatin1().toLower()] = drop;
			} else if (arr.size() == 2) {
				bool ok;
				int ofs = arr.at(0).toInt(&ok, 16);
				QString nm = lst.ns.isEmpty() ? arr.at(1) : (lst.ns + "." + arr.at(1));
				if (ok && (ofs >= 0) && (ofs < ((lst.ram < 0) ? MEM_16K : MEM_64K))) lst.labs.append(qMakePair(ofs, nm));
			} else if (arr.size() == 3) {
				bool ok1, ok2;
				romLabData dat;
				dat.start = arr.at(0).toInt(&ok1, 16);
				dat.end = arr.at(1).toInt(&ok2, 16);
				dat.view = rom_view_code(arr.at(2));
				dat.key = "@" + arr.at(0).toUpper();
				if (ok1 && ok2 && (dat.start >= 0) && (dat.start <= dat.end) && (dat.end < MEM_16K) && dat.view)
					lst.data.append(dat);
			}
		}
		if (!lst.ns.isEmpty()) romSpaces.insert(lst.ns);
		romLists.append(lst);
	}
}

// BASIC48.LD_BYTES -> LD_BYTES; a name with no namespace is itself
static QString short_name(const QString& full) {
	int dot = full.indexOf('.');
	return ((dot > 0) && romSpaces.contains(full.left(dot))) ? full.mid(dot + 1) : full;
}

static void short_add(QHash<QString, QStringList>& map, const QString& full) {
	QString sh = short_name(full);
	if (sh == full) return;
	QStringList& lst = map[sh];
	if (!lst.contains(full)) lst.append(full);
}

// The machine's ROM, as loaded: called under emu_lock whenever it may have
// changed. The same banks as last time change nothing
void labels_rom_scan(Computer* comp) {
	QList<QByteArray> banks;
	if (comp) {
		int size = comp->mem->romMask + 1;
		for (int bank = 0; bank + MEM_16K <= size; bank += MEM_16K)
			banks.append(QCryptographicHash::hash(QByteArray::fromRawData((const char*)comp->mem->romData + bank, MEM_16K),
				QCryptographicHash::Md5).toHex());
	}
	if (comp) romCustomSeen = comp->romCustom;
	if (banks == romBanks) return;
	romBanks = banks;
	romByAbs.clear();
	romByName.clear();
	romShort.clear();
	sysByAbs.clear();
	sysByName.clear();
	sysShort.clear();
	romView = QByteArray(banks.size() * MEM_16K, 0);
	labGen++;
	rom_lists_read();
	QSet<QString> fitted;		// the namespaces of the banks found
	for (int b = 0; b < banks.size(); b++) {
		foreach(const romLabList& lst, romLists) {
			if (lst.ram >= 0) continue;
			QHash<QByteArray, QSet<QString> >::const_iterator it = lst.banks.constFind(banks.at(b));
			if (it == lst.banks.constEnd()) continue;
			fitted.insert(lst.ns);
			for (int i = 0; i < lst.labs.size(); i++) {
				const QString& nm = lst.labs.at(i).second;
				if (it->contains(short_name(nm))) continue;
				int abs = b * MEM_16K + lst.labs.at(i).first;
				romByAbs[abs] = nm;
				QList<int>& cells = romByName[nm];
				if (!cells.contains(abs)) cells.append(abs);	// two lists of a bank may share names
				short_add(romShort, nm);
			}
			foreach(const romLabData& dat, lst.data) {
				if (it->contains(dat.key)) continue;
				memset(romView.data() + b * MEM_16K + dat.start, dat.view, dat.end - dat.start + 1);
			}
		}
	}
	// the system variables of the systems in the machine: cells of bank lst.ram,
	// as the 128K numbers its banks - not the Next, whose RAM cells go another way
	bool next = comp && (comp->hw->id == HW_ZXNEXT);
	foreach(const romLabList& lst, romLists) {
		if ((lst.ram < 0) || next || !fitted.contains(lst.ns)) continue;
		for (int i = 0; i < lst.labs.size(); i++) {
			xAdr xadr;
			xadr.type = MEM_RAM;
			xadr.bank = lst.ram;
			xadr.adr = lst.labs.at(i).first;
			xadr.abs = (lst.ram << 14) | (xadr.adr & 0x3fff);
			const QString& nm = lst.labs.at(i).second;
			sysByAbs[xadr.abs] = nm;
			sysByName[nm] = xadr;
			short_add(sysShort, nm);
		}
	}
	if (!romByAbs.isEmpty())
		xlog(XLG_FILE, XLL_INFO, "ROM labels: %i, system variables: %i", (int)romByAbs.size(), (int)sysByAbs.size());
}

// A ROM a snapshot put in since the last scan (an RZX loads its snapshots as it
// plays): scan again. A byte written into the ROM is no reason to drop a bank's names
void labels_rom_check(Computer* comp) {
	if (comp && (comp->romCustom != romCustomSeen)) labels_rom_scan(comp);
}

// The full name a built-in name stands for: itself, or the one full name a short
// one has among the ROM labels and system variables in effect. Empty when none;
// amb gets the full names when there are more
static QString builtin_full(const QString& nm, QStringList* amb = nullptr) {
	if (conf.dbg.romlabels && romByName.contains(nm)) return nm;
	if (conf.dbg.sysvars && sysByName.contains(nm)) return nm;
	QStringList cands;
	if (conf.dbg.romlabels) cands += romShort.value(nm);
	if (conf.dbg.sysvars) cands += sysShort.value(nm);
	if (cands.size() == 1) return cands.first();
	if (amb) *amb = cands;
	return QString();
}

static bool user_has(const QString& nm) {
	return conf.curlabset && conf.curlabset->list.contains(nm);
}

// A built-in name as the listing shows it: short, unless the short one would mean
// something else when typed (a label of the user's, a name of two systems)
static QString builtin_shown(const QString& full) {
	if (labFull) return full;
	QString sh = short_name(full);
	if ((sh == full) || user_has(sh) || (builtin_full(sh) != full)) return full;
	return sh;
}

// the full names a bare name typed could mean, when it is more than one
QStringList labels_ambiguous(const QString& nm) {
	QStringList amb;
	if (!user_has(nm) && builtin_full(nm, &amb).isEmpty() && (amb.size() > 1)) return amb;
	return QStringList();
}

// built-in names in full while on: Disasm to file
void labels_full(bool on) {
	labFull = on;
}

bool labels_namespace(const QString& ns) {
	return romSpaces.contains(ns);
}

// every cell of a ROM label's name, one a bank it is in
QList<int> labels_rom_cells(const QString& nm) {
	return conf.dbg.romlabels ? romByName.value(builtin_full(nm)) : QList<int>();
}

QStringList labels_rom_names() {
	return conf.dbg.romlabels ? romByName.keys() : QStringList();
}

// the built-in names to complete: ROM labels and system variables in full, and
// the short ones that stand for one
QStringList labels_builtin_names() {
	QStringList res = labels_rom_names();
	QStringList shorts;
	if (conf.dbg.romlabels) shorts += romShort.keys();
	if (conf.dbg.sysvars) {
		res += sysByName.keys();
		shorts += sysShort.keys();
	}
	foreach(const QString& sh, shorts)
		if (!builtin_full(sh).isEmpty()) res.append(sh);
	return res;
}

bool labels_rom_only(const QString& nm) {
	return !builtin_full(nm).isEmpty() && !user_has(nm);
}

// the data mark a ROM list gives a ROM cell, DBG_VIEW_CODE if none
unsigned char labels_rom_view(int abs) {
	if (!conf.dbg.romlabels || !conf.zx || romView.isEmpty()) return DBG_VIEW_CODE;
	abs &= conf.zx->mem->romMask;
	return (abs < romView.size()) ? (unsigned char)romView.at(abs) : DBG_VIEW_CODE;
}

// Where the CPU sees ROM cell abs now, -1 if nowhere. A page number past the
// ROM wraps, as the memory reads it: the Evo pages its 48 BASIC as #3C of 32
static int rom_cpu_adr(int abs) {
	Computer* comp = conf.zx;
	if (!comp) return -1;
	if (comp->hw->memView) return comp_find_xadr(comp, MEM_ROM, abs, MEM_VIEW_READ);
	Memory* mem = comp->mem;
	int base = abs & mem->romMask & ~mem->pgmask;
	for (int i = 0; i < 256; i++) {
		if ((mem->map[i].type == MEM_ROM) && (((mem->map[i].num << mem->pgshift) & mem->romMask) == base))
			return (i << mem->pgshift) | (abs & mem->pgmask);
	}
	return -1;
}

// a ROM cell as the lists name it: wrapped the same way
static int rom_abs(int abs) {
	return conf.zx ? (abs & conf.zx->mem->romMask) : abs;
}

// the cell of a built-in name: a ROM label's, the one paged in now if the name
// is in more banks; a system variable's
xAdr labels_rom_cell(const QString& name) {
	xAdr xadr = {-1, -1, -1, -1};
	QString nm = builtin_full(name);
	if (nm.isEmpty()) return xadr;
	if (!conf.dbg.romlabels || !romByName.contains(nm)) return sysByName.value(nm, xadr);
	const QList<int>& cells = romByName[nm];
	xadr.type = MEM_ROM;
	xadr.abs = cells.first();
	foreach(int abs, cells) {
		int adr = rom_cpu_adr(abs);
		if (adr >= 0) {
			xadr.abs = abs;
			xadr.adr = adr;
			break;
		}
	}
	xadr.bank = xadr.abs >> 14;
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
		if (xadr.type == MEM_ROM) {		// a page past the ROM is the one it wraps to
			xadr.abs = rom_abs(xadr.abs);
			xadr.bank = xadr.abs >> 14;
		}
		set->list[name] = xadr;
		conf.labmap[xadr.type][xadr.abs] = name;
	}
	labGen++;
}

static QString find_label_user(xAdr xadr) {
	QString lab;
	xLabelSet* set = conf.curlabset;
	if (xadr.type == MEM_ROM) xadr.abs = rom_abs(xadr.abs);
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
	return lab;
}

// The user's labels first, then the ROM's; with LAB_SYSVAR a system variable's
// too (operands and tips, never a row of its own). rom: the name is a built-in one
QString find_label(xAdr xadr, bool* rom, int how) {
	QString lab = find_label_user(xadr);
	bool builtin = false;
	if (lab.isEmpty() && (xadr.type == MEM_ROM) && conf.dbg.romlabels) {
		lab = romByAbs.value(rom_abs(xadr.abs));
		builtin = !lab.isEmpty();
	} else if (lab.isEmpty() && (xadr.type == MEM_RAM) && (how & LAB_SYSVAR) && conf.dbg.sysvars) {
		lab = sysByAbs.value(xadr.abs);
		// only where the system has it: bank 5 seen at C000 too is not its variables
		if (!lab.isEmpty() && (xadr.adr >= 0) && ((xadr.adr & 0xffff) != sysByName.value(lab).adr)) lab.clear();
		builtin = !lab.isEmpty();
	}
	if (builtin && !(how & LAB_FULL)) lab = builtin_shown(lab);
	if (rom) *rom = builtin;
	return lab;
}

// A label in ROM whose bank is paged out: where that ROM runs, 0000-3FFF, for a
// typed address and the assembler (CALL TRDOS_FUNCTION from BASIC). Breakpoints
// and conditions go by label_cpu(), only where it is now
xAdr label_home(xAdr xadr) {
	if ((xadr.type == MEM_ROM) && (xadr.adr < 0)) xadr.adr = xadr.abs & 0x3fff;
	return xadr;
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
	if (conf.zx && (xadr.type == MEM_ROM))
		xadr.adr = rom_cpu_adr(xadr.abs);
	else if (conf.zx && (conf.zx->hw->id == HW_ZXNEXT) && (xadr.type == MEM_RAM))
		xadr.adr = comp_find_xadr(conf.zx, xadr.type, xadr.abs, MEM_VIEW_READ);
	// a ROM label's name in other banks too (the Evo's copies of the 48 BASIC): the
	// one paged in now, unless the cell is a label of the user's
	if (conf.zx && (xadr.type == MEM_ROM) && (xadr.adr < 0) && conf.dbg.romlabels) {
		QHash<int, QString>::const_iterator ra = romByAbs.constFind(xadr.abs);
		if ((ra != romByAbs.constEnd()) && !(conf.labmap.contains(MEM_ROM) && conf.labmap[MEM_ROM].contains(xadr.abs))) {
			for (int abs : romByName.constFind(ra.value()).value()) {
				if (abs == xadr.abs) continue;
				int adr = rom_cpu_adr(abs);
				if (adr >= 0) {
					xadr.abs = abs;
					xadr.adr = adr;
					xadr.bank = abs >> 14;
					break;
				}
			}
		}
	}
	return xadr;
}

xAdr find_label(QString nm) {
	return label_cpu(find_label_cell(nm));
}

// A local label, SjASMPlus's "global.local": the length of the part before the
// last dot when a label of that name is loaded, else 0
int label_local_prefix(const QString& nm) {
	int dot = nm.indexOf('.');
	if ((dot > 0) && romSpaces.contains(nm.left(dot))) return dot;	// a namespace, not a label
	int pos = nm.lastIndexOf('.');
	if ((pos < 1) || (pos == nm.size() - 1) || !conf.curlabset) return 0;
	return conf.curlabset->list.contains(nm.left(pos)) ? pos : 0;
}

// The block a cell is in: the nearest label at or before it, in its 16K bank,
// that is not a local one. Empty when there is none near
QString label_block(xAdr xadr) {
	if (!conf.labmap.contains(xadr.type)) return QString();
	const QMap<int, QString>& map = conf.labmap[xadr.type];
	QMap<int, QString>::const_iterator it = map.upperBound(xadr.abs);
	int low = xadr.abs & ~0x3fff;
	for (int i = 0; (i < 256) && (it != map.constBegin()); i++) {
		it--;
		if (it.key() < low) break;
		if (!label_local_prefix(it.value())) return it.value();
	}
	return QString();
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
