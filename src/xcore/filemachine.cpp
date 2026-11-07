#include "filemachine.h"
#include "xcore.h"
#include "autostart.h"
#include "../filer.h"

#include <map>
#include <algorithm>
#include <QRegularExpression>

// Which machine a file is opened on: what each format needs, what the user
// wants done about it, and the decision the two make together.

static const xFileMac fm_tab[] = {
	{FL_SNA, "sna", "SNA snapshot", FMN_SNAPSHOT, NULL, snaGetHardware},
	{FL_Z80, "z80", "Z80 snapshot", FMN_SNAPSHOT, NULL, z80GetHardware},
	{FL_SZX, "szx", "SZX snapshot", FMN_EXACT, NULL, szxGetHardware},
	{FL_SPG, "spg", "SPG snapshot", FMN_TSCONF, "evo-tsconf", NULL},
	{FL_NEX, "nex", "NEX program", FMN_NEXT, "zxnext", NULL},
	{FL_SNX, "snx", "SNX snapshot", FMN_NEXT, "zxnext", NULL},
	{FL_RZX, "rzx", "RZX playback", FMN_RZX, NULL, rzxGetHardware},
	{FL_TAP, "tap", "TAP tape", FMN_ANY, NULL, NULL},
	{FL_TZX, "tzx", "TZX tape", FMN_ANY, NULL, NULL},
	{FL_WAV, "wav", "WAV tape", FMN_ANY, NULL, NULL},
	{FL_TRD, "trd", "TRD disk", FMN_TRDOS, "pent", NULL},
	{FL_SCL, "scl", "SCL disk", FMN_TRDOS, "pent", NULL},
	{FL_FDI, "fdi", "FDI disk", FMN_TRDOS, "pent", NULL},
	{FL_TD0, "td0", "TD0 disk", FMN_TRDOS, "pent", NULL},
	{FL_UDI, "udi", "UDI disk", FMN_TRDOS, "pent", NULL},
	{FL_DSK, "dsk", "DSK disk", FMN_PLUS3DOS, "zxplus3", NULL},
	{0, NULL, NULL, 0, NULL, NULL}
};

// the machine that is exactly what a snapshot was taken on
static const struct {
	int hw;
	const char* target;
} fm_snap_tab[] = {
	{SNAP_HW_48K, "zx48"},
	{SNAP_HW_128K, "zx128"},
	{SNAP_HW_PLUS2, "zxplus2"},
	{SNAP_HW_PLUS2A, "zxplus2a"},
	{SNAP_HW_PLUS3, "zxplus3"},
	{SNAP_HW_PENTAGON, "pent"},
	{SNAP_HW_SCORPION, "scorp"},
	{SNAP_HW_PENT512, "pent"},
	{SNAP_HW_PENT1024, "pent1024"},
	{SNAP_HW_UNKNOWN, NULL}
};

std::string fm_snap_target(int snap) {
	snap = SNAP_HW_OF(snap);
	int i = 0;
	while ((fm_snap_tab[i].hw != SNAP_HW_UNKNOWN) && (fm_snap_tab[i].hw != snap))
		i++;
	return fm_snap_tab[i].target ? fm_snap_tab[i].target : "";
}

static const char* fm_need_tab[] = {
	"any machine", "TSConf", "ZX Spectrum Next", "Beta Disk (TR-DOS)", "+3 disk drive", "read from the file",
	"exactly the one it was recorded on", "exactly the one it was taken on"
};

static std::map<std::string, std::string> fm_pref_map;

const xFileMac* fm_rows() {
	return fm_tab;
}

static const xFileMac* fm_row(int ftype) {
	for (int i = 0; fm_tab[i].key; i++) {
		if (fm_tab[i].ftype == ftype) return &fm_tab[i];
	}
	return NULL;
}

const char* fm_need_text(int need) {
	if ((need < FMN_ANY) || (need > FMN_EXACT)) return "";
	return fm_need_tab[need];
}

// a disk has to be started, not just read, so it asks the one table that knows;
// model is the machine's snapModel, which tells a +2 from a 128K
static bool fm_takes(int hwid, int model, int dif, int need, int snap) {
	switch (need) {
		case FMN_TSCONF: return hwid == HW_TSLAB;
		case FMN_NEXT: return hwid == HW_ZXNEXT;
		case FMN_TRDOS: return autostart_can(hwid, dif, AS_DISK);
		case FMN_PLUS3DOS: return autostart_can(hwid, dif, AS_DISK3);
		case FMN_SNAPSHOT: return snapHwRuns(snap, hwid, model);
		case FMN_RZX:
		case FMN_EXACT: return snapHwIs(snap, hwid);
	}
	return true;
}

// A snapshot names the model it was taken on - but a .sna, whose size only
// says 48K or 128K, and a disk names a family at most
static bool fm_names_model(const xFileMac* row) {
	return row->probe && (row->ftype != FL_SNA);
}

// An SZX stands the machine at a point in its frame, so it is loaded on the
// model it names: a core shared by several models is not enough, unless the
// machine is one of the user's own rather than a model of the table.
static bool fm_exact_on(const std::string& id, int snap, const std::string& target) {
	if (target.empty() || (target == id)) return true;
	if (SNAP_HW_OF(snap) >= SNAP_HW_CORE) return !xm_find(target);
	for (int i = 0; fm_snap_tab[i].target; i++) {
		if (id == fm_snap_tab[i].target) return false;
	}
	return true;
}

// the running machine is asked as it runs: what the user changed on it since
// it was set is not in its definition yet
bool fm_runs(const std::string& id, int need, int snap) {
	if ((id == conf.macId) && conf.zx)
		return fm_takes(conf.zx->hw->id, conf.zx->snapModel, conf.zx->dif->cfg, need, snap);	// out under a divMMC is still its
	const xMachine* mac = xm_find(id);
	if (!mac) return false;
	HardWare* hw = findHardware(mac->hw.c_str());
	return hw && fm_takes(hw->id, mac->snapModel, mac->disk, need, snap);
}

void fm_clear() {
	fm_pref_map.clear();
}

std::string fm_pref(const std::string& key) {
	std::map<std::string, std::string>::iterator it = fm_pref_map.find(key);
	return (it == fm_pref_map.end()) ? std::string(FM_AUTO) : it->second;
}

void fm_load(const std::string& key, const std::string& val) {
	int i = 0;
	while (fm_tab[i].key && (key != fm_tab[i].key))
		i++;
	if (!fm_tab[i].key) return;			// no such row
	if (val.empty() || (val == FM_AUTO)) {
		fm_pref_map.erase(key);
	} else {
		fm_pref_map[key] = val;
	}
}

void fm_save(FILE* file) {
	fprintf(file, "\n[FILETYPES]\n\n");
	for (int i = 0; fm_tab[i].key; i++) {
		std::string val = fm_pref(fm_tab[i].key);
		if (val != FM_AUTO)
			fprintf(file, "%s = %s\n", fm_tab[i].key, val.c_str());
	}
}

// a 16K image from the ROM folder, empty if it is not there
static std::vector<unsigned char> fm_rom_image(const char* name) {
	std::vector<unsigned char> img(MEM_16K);
	FILE* file = fopen((conf.path.romDir + SLASH + name).c_str(), "rb");
	if (!file) return std::vector<unsigned char>();
	if (fread(img.data(), 1, MEM_16K, file) != MEM_16K) img.clear();
	fclose(file);
	return img;
}

// The 128K editor and its clones are one code; a menu snapshot still tells them
// apart by its screen, the menu drawn in the ROM font: the Pentagon's has TR-DOS
// where the 128K's has Tape Tester. The ROM holding the most words of the
// screen wins; a tie says nothing.
static int fm_sna_clone(const char* path) {
	static const char* names[] = {"128-0.rom", "128p-0.rom", "256s-0.rom"};
	static const int snaps[] = {SNAP_HW_128K | SNAP_IN_ROM, SNAP_HW_PENTAGON | SNAP_IN_ROM, SNAP_HW_SCORPION};
	std::vector<unsigned char> font = fm_rom_image("48.rom");
	if (font.empty()) return -1;
	char text[24 * 33];
	sna_screen_text(path, font.data() + 0x3d00, text);
	std::vector<std::string> words;
	foreach(QString w, QString(text).split(QRegularExpression("\\s+"), X_SkipEmptyParts))
		if (w.size() > 2) words.push_back(w.toStdString());
	int best = -1;
	int top = 0;
	for (int i = 0; i < 3; i++) {
		std::vector<unsigned char> rom = fm_rom_image(names[i]);
		if (rom.empty()) continue;
		std::string txt(rom.begin(), rom.end());
		for (char& ch : txt) ch &= 0x7f;		// a string's last letter has bit 7 set
		int n = 0;
		for (const std::string& w : words)
			if (txt.find(w) != std::string::npos) n++;
		if (n > top) {
			top = n;
			best = i;
		} else if (n == top) {
			best = -1;
		}
	}
	if (best >= 0) xlog(XLG_FILE, XLL_INFO, "sna: its screen is the menu of %s", names[best]);
	return (best < 0) ? -1 : snaps[best];
}

// A 128K .sna in its editor names no model, so the editor is told from the file
// against the ones in the ROM folder, then the model where the file can tell it;
// a file that does not tell runs anywhere a 128K does, as the user picks.
static int fm_sna_guess(const char* path) {
	static const char* names[] = {"128-0.rom", "plus2-0.rom", "plus3-0.rom"};
	std::vector<unsigned char> img[3];
	const unsigned char* eds[3];
	int idx[3];
	int n = 0;
	for (int i = 0; i < 3; i++) {
		img[i] = fm_rom_image(names[i]);
		if (img[i].empty()) continue;
		eds[n] = img[i].data();
		idx[n++] = i;
	}
	int ed = (n > 0) ? sna_editor_of(path, eds, n) : -1;
	xlog(XLG_FILE, XLL_INFO, "sna in its editor: %s", (ed < 0) ? "no model told" : names[idx[ed]]);
	if (ed < 0) return SNAP_HW_128K;
	switch (idx[ed]) {
		case 1: return SNAP_HW_PLUS2 | SNAP_IN_ROM | SNAP_FAMILY;
		case 2: return sna_plus3_drive(path) ? SNAP_HW_PLUS3 : SNAP_HW_PLUS2A;
	}
	int clone = fm_sna_clone(path);
	return (clone < 0) ? (SNAP_HW_128K | SNAP_IN_ROM | SNAP_FAMILY) : clone;
}

xFileMacPick fm_pick(int ftype, const char* path) {
	xFileMacPick pick;
	const xFileMac* row = fm_row(ftype);
	if (!row) return pick;
	std::string pref = fm_pref(row->key);
	if (pref == FM_KEEP) return pick;
	int snap = SNAP_HW_UNKNOWN;
	std::string target = row->target ? row->target : "";
	if (row->probe) {
		snap = row->probe(path);
		if ((ftype == FL_SNA) && (snap & SNAP_IN_ROM))
			snap = fm_sna_guess(path);
		target = fm_snap_target(snap);
		// a file of ours names the very machine it was taken on
		if ((SNAP_HW_OF(snap) >= SNAP_HW_CORE) && (ftype == FL_SZX)) {
			char id[256];
			szx_machine_of(path, id, sizeof(id));
			target = id;
		}
	}
	// one machine for the whole format, unless it cannot take this very file
	if ((pref != FM_AUTO) && (pref != FM_ASK) && fm_runs(pref, row->need, snap)) {
		if (pref != conf.macId) pick.target = pref;
		return pick;
	}
	auto fits = [&](const std::string& id) {
		return fm_runs(id, row->need, snap) && ((row->need != FMN_EXACT) || fm_exact_on(id, snap, target));
	};
	// the machines but the running one that can take the file, in this order
	std::vector<std::string> can;
	auto add = [&](const std::string& id) {
		if (!id.empty() && (id != conf.macId) && (std::find(can.begin(), can.end(), id) == can.end())
				&& fits(id))
			can.push_back(id);
	};
	// Ask, whenever there is a choice: not when the running machine is the model
	// the file names, nor when only one machine can take it. A file that names
	// none has the machine Auto would go back to on top, and Keep for the answer
	// when the running one can take it.
	if (pref == FM_ASK) {
		bool named = fm_names_model(row) && !target.empty();
		if (named && (target == conf.macId)) return pick;
		pick.runs = fits(conf.macId);
		if (!named && fits(conf.macBase)) add(conf.macBase);
		add(target);
		foreach(const xMachine& mac, xm_list())
			add(mac.id);
		if (can.size() + (pick.runs ? 1 : 0) < 2) {
			if (!pick.runs && !can.empty()) pick.target = can.front();
			return pick;
		}
		pick.ask = can;
		pick.keep = !named && pick.runs;
		xlog(XLG_FILE, XLL_INFO, "%s on %s: asking", row->key, conf.macId.c_str());
		return pick;
	}
	// The user's own machine when it can take the file: a machine a file
	// switched to was for that file. Then the one running, if it can.
	if (fits(conf.macBase)) {
		if (conf.macBase != conf.macId) {
			pick.target = conf.macBase;
			xlog(XLG_FILE, XLL_INFO, "%s: back from %s to %s", row->key, conf.macId.c_str(), pick.target.c_str());
		}
		return pick;
	}
	if (fits(conf.macId)) return pick;
	// the one the file names, then every other one that can take it
	add(target);
	foreach(const xMachine& mac, xm_list())
		add(mac.id);
	if (can.empty()) return pick;
	pick.target = can.front();
	xlog(XLG_FILE, XLL_INFO, "%s does not run on %s: %s", row->key, conf.macId.c_str(), pick.target.c_str());
	return pick;
}
