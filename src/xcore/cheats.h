#pragma once

#include <QString>
#include <QList>
#include <QStringList>

#include "../libxpeccy/spectrum.h"
#include "../libxpeccy/pokes.h"

// Cheats: the trainers of one .pok file and a list of pokes made by hand, ticked on
// and off in the running machine. Session state only. Every call takes emu_lock: a
// reset, a load and a rewind change memory on the emulation thread.

enum {
	CHT_RZX = 100,		// cheats_set: an RZX is playing, nothing goes in
	CHT_NO_LABEL		// cheats_set, xCheatRow.mark: a label a poke names is not loaded
};

// A --poke address given as a label (+/- an offset): the place is looked up when the
// trainer is ticked, so a rebuilt program's labels move it
struct xPokeLabel {
	QString name;		// empty: the poke's own address
	int off;
};
typedef QList<QList<xPokeLabel>> xPokeLabels;		// a list a trainer, an item a poke

// What the Cheats window shows of a trainer
struct xCheatRow {
	QString name;
	int on;
	int why;		// POK_BAD_*: the reader spoiled it
	int line;
	int ask;		// has a value of 256
	int val;		// ...and the one the user gave, -1 none yet
	int mark;		// POK_FITS, POK_UNCHECKED (no org known), POK_MISMATCH once found,
				// POK_NO_PAGE/POK_NO_RAM where this machine has no place for it,
				// CHT_NO_LABEL
	QList<xPoke> pokes;
	QStringList labs;	// a poke's label as given ("lives+1"), or empty
	QString miss;		// the label not loaded (mark CHT_NO_LABEL)
};

// open a .pok: 0, else the reader's POK_ERR_* (-1 can't read the file). picked = found
// beside an image, which an image without one drops again
int cheats_open(const QString& path, bool picked);
// after an image opened: POKES/<name>.pok or <name>.pok beside it, any case; the same
// image again turns every cheat off, bytes back
void cheats_pick(const QString& image);
// the command line's trainers, taken over, for the image it opened (see cheats.cpp)
bool cheats_add(xPokFile* pf, const xPokeLabels& labs, bool alone, const QString& image);
QString cheats_file();
bool cheats_picked();
bool cheats_cli();		// the list has the command line's trainers
void cheats_clear();		// off (bytes back) and gone, the hand list too

// on/off with the bytes back; a known org that is not in memory gives POK_MISMATCH and
// nothing goes in unless forced. idx -1 = the hand list.
int cheats_set(Computer*, int idx, bool on, bool force);
void cheats_set_value(int idx, int val);
// one more poke by hand at a cpu address, the hand list turned on
int cheats_hand_poke(Computer*, int adr, int val);
void cheats_hand_clear(Computer*);

QList<xCheatRow> cheats_rows(Computer*);	// the file's trainers, then the hand list
int cheats_count();
int cheats_serial();		// moves on every change the window should show

// The machine was replaced (reset, load, switch): everything off, nothing written.
void cheats_forget();
// It went back in time (a rewind, fast loading's wind back): a trainer whose bytes are
// the saved ones again goes back in, one with other bytes goes off.
void cheats_rollback(Computer*);
