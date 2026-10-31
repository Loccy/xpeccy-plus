#pragma once

// Saves of a game: the quick saves and the autosaves of the image the user
// opened, each an .szx with a picture beside it, in a folder of the game's own
// under <saves>/ - named after the game and the hash of its bytes, so the same
// game bare, in an archive or moved finds the same folder. With no game, or
// with per-game saves off, they go to the machine's folder instead.
//
// The folder holds game.json (the game's name, its hash, the paths it was
// opened from) and kind-YYYYMMDD-HHMMSS-zzz.szx + .png: the name is the order,
// so a ring needs no counters.

#include <QDateTime>
#include <QList>
#include <QString>

#include "../libxpeccy/spectrum.h"

enum {
	SAVE_QUICK = 0,
	SAVE_EXIT,		// the autosave taken on exit
	SAVE_TIMED,		// the periodic one
	SAVE_KINDS
};

typedef struct {
	QString path;		// the .szx
	QString pic;		// its picture, empty when there is none
	QDateTime time;
	int kind;
	qint64 size;		// bytes, the picture's too
} xSave;

// The game: what the user opened into drive A, as a tape or as a snapshot.
// The bytes as opened are hashed here (an entry's for an archive path).
void game_set(const QString& image);
// the game of a folder of saves, as its json has it: a save of it is going in
void game_set_folder(const QString& dir);
void game_forget();
// as it is, to put back (undoing a quick load)
typedef struct {
	QString image;
	QString hash;
	QString name;
	int src;
} xGame;
xGame game_now();
void game_put(const xGame&);
// forgets it once the tape or the disk it was opened as is out
void game_check(Computer*);
QString game_image();			// empty: none
QString game_name();			// what the folder and the menus call it

// where all of them are: the setting, or <config>/saves
QString saves_root();
// The folder a new save goes to: the game's, or the machine's when there is no
// game (or per-game saves are off). make: create it with its json; empty when
// it is not there and make is false.
QString saves_dir(bool make);
// the game's own folder, empty when it has none yet (no save taken)
QString saves_game_dir();
// a folder's saves, newest first; kind -1 for all
QList<xSave> saves_list(const QString& dir, int kind = -1);
// the name of a new save of that kind in dir, by the time now
QString saves_new_path(const QString& dir, int kind);
// the oldest of that kind past keep, gone with their pictures
void saves_trim(const QString& dir, int kind, int keep);
void saves_remove(const xSave& sv);
// "Today 21:05:13", "Yesterday ...", the date otherwise
QString saves_time_text(const QDateTime& t);
QString saves_kind_name(int kind);
// the image a folder's game was opened from last, empty for a machine's folder
QString saves_folder_image(const QString& dir);
