#include <atomic>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "cheats.h"
#include "xcore.h"
#include "../libxpeccy/filetypes/filetypes.h"

struct chState {
	int on;
	int val;
	int mismatch;
};

static xPokFile ch_file = {0, 0, NULL};
static std::vector<chState> ch_state;		// one per trainer of ch_file
static xTrainer ch_hand;
static chState ch_hand_st = {0, -1, 0};
static QString ch_path;
static bool ch_picked = false;
static bool ch_cli = false;		// trainers from the command line in the list
static std::atomic<int> ch_serial(0);

class chLock {
	public:
		chLock() {emu_lock();}
		~chLock() {emu_unlock();}
};

static void ch_changed() {
	ch_serial.fetch_add(1);
}

static xTrainer* ch_trainer(int idx, chState** st) {
	if (idx < 0) {
		*st = &ch_hand_st;
		return &ch_hand;
	}
	if (idx >= ch_file.cnt) return NULL;
	*st = &ch_state[idx];
	return &ch_file.list[idx];
}

// the file's trainers off, bytes back, and the file gone
static void ch_drop_file(Computer* comp) {
	for (int i = 0; i < ch_file.cnt; i++) {
		if (ch_state[i].on && comp) {
			pok_undo(comp, &ch_file.list[i]);
			rzx_rec_touch();
		}
	}
	pok_free(&ch_file);
	ch_state.clear();
	ch_path.clear();
	ch_picked = false;
	ch_cli = false;
}

static Computer* ch_comp = NULL;	// the machine the trainers that are on went into

int cheats_open(const QString& path, bool picked) {
	QFile f(path);
	if (f.size() > POK_FILE_MAX) return POK_ERR_SIZE;
	if (!f.open(QFile::ReadOnly)) return -1;
	QByteArray buf = f.readAll();
	xPokFile pf;
	int err = pok_parse(buf.constData(), buf.size(), &pf);
	if (err != POK_OK) return err;
	chLock lock;
	ch_drop_file(ch_comp);
	ch_file = pf;
	ch_state.assign(pf.cnt, chState{0, -1, 0});
	ch_path = path;
	ch_picked = picked;
	ch_changed();
	return POK_OK;
}

// Fuse's and Spectaculator's rule, and how every archive with pokes has it:
// POKES/NAME.POK beside the image, else NAME.pok beside it
void cheats_pick(const QString& image) {
	QFileInfo inf(image);
	QString want = inf.completeBaseName() + ".pok";
	QDir dir = inf.dir();
	QString found;
	foreach(QString sub, dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
		if (sub.compare("pokes", Qt::CaseInsensitive)) continue;
		foreach(QString name, QDir(dir.filePath(sub)).entryList(QDir::Files)) {
			if (!name.compare(want, Qt::CaseInsensitive))
				found = dir.filePath(sub + "/" + name);
		}
	}
	if (found.isEmpty()) {
		foreach(QString name, dir.entryList(QDir::Files)) {
			if (!name.compare(want, Qt::CaseInsensitive))
				found = dir.filePath(name);
		}
	}
	if (!found.isEmpty() && (cheats_open(found, true) == POK_OK))
		return;
	chLock lock;
	if (ch_picked) {
		ch_drop_file(ch_comp);
		ch_changed();
	}
}

// The command line's trainers, after a file given there (alone: the list emptied first,
// so they win over the image's own). Alone they go with the game, as a found file does;
// after a file they stay with it.
bool cheats_add(xPokFile* pf, bool alone) {
	chLock lock;
	if (alone) ch_drop_file(ch_comp);
	int cnt = ch_file.cnt + pf->cnt;
	if (cnt > ch_file.cap) {
		xTrainer* nl = (xTrainer*)realloc(ch_file.list, cnt * sizeof(xTrainer));
		if (!nl) {
			pok_free(pf);
			ch_changed();		// the list may be gone already
			return false;
		}
		ch_file.list = nl;
		ch_file.cap = cnt;
	}
	memcpy(ch_file.list + ch_file.cnt, pf->list, pf->cnt * sizeof(xTrainer));	// the pokes move with them
	ch_file.cnt = cnt;
	ch_state.resize(cnt, chState{0, -1, 0});
	free(pf->list);
	memset(pf, 0, sizeof(xPokFile));
	if (alone) ch_picked = true;
	ch_cli = true;
	ch_changed();
	return true;
}

QString cheats_file() {
	chLock lock;
	return ch_path;
}

bool cheats_picked() {
	chLock lock;
	return ch_picked;
}

bool cheats_cli() {
	chLock lock;
	return ch_cli;
}

void cheats_clear() {
	chLock lock;
	ch_drop_file(ch_comp);
	if (ch_hand_st.on && ch_comp) {
		pok_undo(ch_comp, &ch_hand);
		rzx_rec_touch();
	}
	pok_trainer_free(&ch_hand);
	ch_hand_st = {0, -1, 0};
	ch_changed();
}

int cheats_set(Computer* comp, int idx, bool on, bool force) {
	chLock lock;
	chState* st;
	xTrainer* tr = ch_trainer(idx, &st);
	if (!tr || (tr->why && on) || (st->on == on)) return POK_FITS;
	if (on && tr->ask && (st->val < 0)) return POK_FITS;	// the window asks for the value first
	if (comp->rzx.play) return CHT_RZX;
	if (on) {
		int res = pok_check(comp, tr);
		if (res == POK_MISMATCH) {
			st->mismatch = 1;
			ch_changed();
			if (!force) return res;
		} else if (res != POK_FITS && res != POK_UNCHECKED) {
			return res;
		}
		res = pok_apply(comp, tr, st->val);
		if (res != POK_FITS) return res;
		ch_comp = comp;
	} else {
		pok_undo(comp, tr);
	}
	st->on = on;
	rzx_rec_touch();
	ch_changed();
	return POK_FITS;
}

void cheats_set_value(int idx, int val) {
	chLock lock;
	chState* st;
	xTrainer* tr = ch_trainer(idx, &st);
	if (!tr || st->on) return;
	st->val = val;
	ch_changed();
}

int cheats_hand_poke(Computer* comp, int adr, int val) {
	chLock lock;
	if (comp->rzx.play) return CHT_RZX;
	if (!ch_hand_st.on && ch_hand.cnt) {
		if (pok_apply(comp, &ch_hand, 0) != POK_FITS)
			pok_trainer_free(&ch_hand);	// no place for them now: start over
	}
	int res = pok_poke(comp, &ch_hand, 8, adr, val);
	if (res == POK_FITS) {
		ch_hand_st.on = 1;
		ch_comp = comp;
	}
	rzx_rec_touch();
	ch_changed();
	return res;
}

void cheats_hand_clear(Computer* comp) {
	chLock lock;
	if (ch_hand_st.on && !comp->rzx.play) {
		pok_undo(comp, &ch_hand);
		rzx_rec_touch();
	}
	pok_trainer_free(&ch_hand);
	ch_hand_st = {0, -1, 0};
	ch_changed();
}

static xCheatRow ch_row(Computer* comp, const xTrainer* tr, const chState* st) {
	xCheatRow r;
	r.name = QString::fromLatin1(tr->name);
	r.on = st->on;
	r.why = tr->why;
	r.line = tr->line;
	r.ask = tr->ask;
	r.val = st->val;
	int res = tr->why ? POK_FITS : pok_check(comp, tr);
	if (res == POK_NO_PAGE || res == POK_NO_RAM) {
		r.mark = res;
	} else if (st->mismatch) {
		r.mark = POK_MISMATCH;
	} else {
		r.mark = POK_UNCHECKED;
		for (int i = 0; i < tr->cnt; i++)
			if (tr->pokes[i].org) r.mark = POK_FITS;
	}
	for (int i = 0; i < tr->cnt; i++)
		r.pokes.append(tr->pokes[i]);
	return r;
}

QList<xCheatRow> cheats_rows(Computer* comp) {
	chLock lock;
	QList<xCheatRow> rows;
	for (int i = 0; i < ch_file.cnt; i++)
		rows.append(ch_row(comp, &ch_file.list[i], &ch_state[i]));
	if (ch_hand.cnt) {
		xCheatRow r = ch_row(comp, &ch_hand, &ch_hand_st);
		r.mark = POK_FITS;
		rows.append(r);
	}
	return rows;
}

int cheats_count() {
	chLock lock;
	return ch_file.cnt;
}

int cheats_serial() {
	return ch_serial.load();
}

static void ch_off(xTrainer* tr, chState* st) {
	st->on = 0;
	st->mismatch = 0;
	for (int i = 0; i < tr->cnt; i++)
		tr->pokes[i].saved = -1;
}

void cheats_forget() {
	chLock lock;
	for (int i = 0; i < ch_file.cnt; i++)
		ch_off(&ch_file.list[i], &ch_state[i]);
	ch_off(&ch_hand, &ch_hand_st);
	ch_comp = NULL;
	ch_changed();
}

static void ch_back(Computer* comp, xTrainer* tr, chState* st, const char* name) {
	if (!st->on) return;
	switch (pok_where(comp, tr, st->val)) {
		case POK_AT_ON: break;
		case POK_AT_OFF:
			pok_apply(comp, tr, st->val);
			rzx_rec_touch();
			xlog(XLG_CORE, XLL_INFO, "cheats: '%s' poked again after going back", name);
			break;
		default:
			ch_off(tr, st);
			xlog(XLG_CORE, XLL_INFO, "cheats: '%s' off, its bytes are not there after going back", name);
			break;
	}
}

void cheats_rollback(Computer* comp) {
	chLock lock;
	for (int i = 0; i < ch_file.cnt; i++)
		ch_back(comp, &ch_file.list[i], &ch_state[i], ch_file.list[i].name);
	ch_back(comp, &ch_hand, &ch_hand_st, "custom pokes");
	ch_changed();
}
