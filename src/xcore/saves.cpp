#include "saves.h"
#include "arcpath.h"
#include "xcore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

// past this an image is a hard disk or a card, keyed by its path: reading
// gigabytes on every open is no way to find a folder
#define GAME_HASH_MAX	(16 << 20)

static const char* save_prefix[SAVE_KINDS] = {"quick", "exit", "auto"};

// what holds the game, GSRC_*: looked at once the media are in
enum {GSRC_NONE, GSRC_TAPE, GSRC_DISK, GSRC_LOOK};

static xGame game = {QString(), QString(), QString(), GSRC_NONE};	// image: as opened; hash: sha-1, hex

QString saves_root() {
	if (!conf.saves.dir.isEmpty()) return conf.saves.dir;
	return QDir(QString::fromLocal8Bit(conf.path.confDir.c_str())).filePath("saves");
}

// a name any file system takes
static QString saves_clean(QString name) {
	for (QChar& ch : name) {
		if ((ch.unicode() < 32) || QString("<>:\"|?*\\/").contains(ch)) ch = '_';
	}
	name = name.trimmed();
	while (name.endsWith('.')) name.chop(1);
	if (name.length() > 60) name = name.left(60).trimmed();
	return name.isEmpty() ? QString("game") : name;
}

static QString game_hash(const QString& image) {
	QString err;
	QString local = arc_local(image, &err);
	QFile file(local);
	QCryptographicHash sha(QCryptographicHash::Sha1);
	if (!local.isEmpty() && (file.size() <= GAME_HASH_MAX) && file.open(QIODevice::ReadOnly)) {
		sha.addData(&file);
	} else {
		// a path, made to differ from any content's hash
		sha.addData(QByteArray("path:") + QDir::fromNativeSeparators(image).toUtf8());
	}
	return QString::fromLatin1(sha.result().toHex());
}

static QJsonObject saves_json(const QString& dir) {
	QFile file(dir + "/game.json");
	if (!file.open(QIODevice::ReadOnly)) return QJsonObject();
	return QJsonDocument::fromJson(file.readAll()).object();
}

static void saves_json_write(const QString& dir, const QJsonObject& obj) {
	QFile file(dir + "/game.json");
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		file.write(QJsonDocument(obj).toJson());
}

void game_set(const QString& image) {
	if (image.isEmpty()) {
		game_forget();
		return;
	}
	game.image = image;
	game.hash = game_hash(image);
	QString entry;
	arc_split(image, nullptr, &entry);
	game.name = saves_clean(QFileInfo(entry.isEmpty() ? image : entry).completeBaseName());
	game.src = GSRC_LOOK;
	// a folder that is there already learns this path too
	QString dir = saves_game_dir();
	if (!dir.isEmpty()) saves_dir(true);
}

void game_set_folder(const QString& dir) {
	QJsonObject obj = saves_json(dir);
	QJsonArray paths = obj.value("images").toArray();
	if (obj.value("hash").toString().isEmpty() || paths.isEmpty()) {
		game_forget();			// a machine's folder
		return;
	}
	game.image = paths.at(0).toString();
	game.hash = obj.value("hash").toString();
	game.name = obj.value("name").toString();
	game.src = GSRC_LOOK;
}

xGame game_now() {
	return game;
}

void game_put(const xGame& g) {
	game = g;
}

void game_forget() {
	game.image.clear();
	game.hash.clear();
	game.name.clear();
	game.src = GSRC_NONE;
}

// the tape or the disk it was opened as has left its place
void game_check(Computer* comp) {
	if (game.src == GSRC_LOOK) {
		QByteArray raw = game.image.toLocal8Bit();
		game.src = (comp->tape->path && (raw == comp->tape->path)) ? GSRC_TAPE
			: (comp->dif->flp[0]->path && (raw == comp->dif->flp[0]->path)) ? GSRC_DISK : GSRC_NONE;
	}
	if (game.src == GSRC_NONE) return;
	const char* raw = (game.src == GSRC_TAPE) ? comp->tape->path : comp->dif->flp[0]->path;
	if (!raw || (game.image.toLocal8Bit() != raw))
		game_forget();
}

QString game_image() {
	return game.image;
}

QString game_name() {
	return game.name;
}

static QString saves_machine_dir() {
	return QDir(saves_root()).filePath("machine - " + saves_clean(QString::fromLocal8Bit(conf.macId.c_str())));
}

// the folder ending in the hash, whatever the name it was made with
QString saves_game_dir() {
	if (game.hash.isEmpty()) return QString();
	const QString tail = " - " + game.hash.left(8);
	QDir root(saves_root());
	const QStringList found = root.entryList({"*" + tail}, QDir::Dirs | QDir::NoDotAndDotDot);
	for (const QString& name : found) {
		QFile file(root.filePath(name) + "/game.json");
		if (!file.open(QIODevice::ReadOnly)) continue;
		QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
		if (obj.value("hash").toString() == game.hash) return root.filePath(name);
	}
	return QString();
}

// the machine's one quick save from before there were folders
static QString saves_old_quick() {
	return QDir(QString::fromLocal8Bit(conf.path.confDir.c_str())).filePath(
		"quick/" + QString::fromLocal8Bit(conf.macId.c_str()) + ".szx");
}

// ...which becomes the oldest quick save of the machine's folder
static void saves_migrate(const QString& dir) {
	QString old = saves_old_quick();
	if (!QFileInfo::exists(old)) return;
	QDateTime t = QFileInfo(old).lastModified();
	QString to = QDir(dir).filePath(QString("quick-%1.szx").arg(t.toString("yyyyMMdd-HHmmss-zzz")));
	if (QFile::rename(old, to)) return;
	if (QFile::copy(old, to)) QFile::remove(old);
}

QString saves_dir(bool make) {
	if (game.hash.isEmpty() || !conf.saves.perGame) {
		QString dir = saves_machine_dir();
		if (!make && !QFileInfo(dir).isDir() && !QFileInfo::exists(saves_old_quick())) return QString();
		if (!QDir().mkpath(dir)) return QString();
		if (!QFileInfo::exists(dir + "/game.json")) {
			QJsonObject obj;
			obj.insert("machine", QString::fromLocal8Bit(conf.macId.c_str()));
			saves_json_write(dir, obj);
		}
		saves_migrate(dir);
		return dir;
	}
	QString dir = saves_game_dir();
	if (dir.isEmpty()) {
		if (!make) return QString();
		dir = QDir(saves_root()).filePath(game.name + " - " + game.hash.left(8));
		if (!QDir().mkpath(dir)) return QString();
	}
	if (make) {
		QJsonObject obj = saves_json(dir);
		QJsonArray paths = obj.value("images").toArray();
		const QString img = QDir::fromNativeSeparators(game.image);
		for (int i = paths.size() - 1; i >= 0; i--) {
			if (paths.at(i).toString() == img) paths.removeAt(i);
		}
		paths.prepend(img);			// the last one first
		while (paths.size() > 8) paths.removeLast();
		obj.insert("name", game.name);
		obj.insert("hash", game.hash);
		obj.insert("images", paths);
		obj.insert("machine", QString::fromLocal8Bit(conf.macId.c_str()));
		saves_json_write(dir, obj);
	}
	return dir;
}

QString saves_folder_image(const QString& dir) {
	QJsonArray paths = saves_json(dir).value("images").toArray();
	return paths.isEmpty() ? QString() : paths.at(0).toString();
}

static int saves_kind_of(const QString& name) {
	for (int k = 0; k < SAVE_KINDS; k++) {
		if (name.startsWith(QString(save_prefix[k]) + "-")) return k;
	}
	return -1;
}

QList<xSave> saves_list(const QString& dir, int kind) {
	QList<xSave> res;
	if (dir.isEmpty()) return res;
	QDir d(dir);
	const QFileInfoList files = d.entryInfoList({"*.szx"}, QDir::Files, QDir::Name | QDir::Reversed);
	for (const QFileInfo& fi : files) {
		int k = saves_kind_of(fi.fileName());
		if ((k < 0) || ((kind >= 0) && (k != kind))) continue;
		xSave sv;
		sv.path = fi.filePath();
		sv.kind = k;
		QString stamp = fi.completeBaseName().mid(strlen(save_prefix[k]) + 1);
		sv.time = QDateTime::fromString(stamp, "yyyyMMdd-HHmmss-zzz");
		if (!sv.time.isValid()) sv.time = fi.lastModified();
		sv.size = fi.size();
		QString pic = d.filePath(fi.completeBaseName() + ".png");
		if (QFileInfo::exists(pic)) {
			sv.pic = pic;
			sv.size += QFileInfo(pic).size();
		}
		res.append(sv);
	}
	// all kinds together: by time, not by name
	std::stable_sort(res.begin(), res.end(), [](const xSave& a, const xSave& b) {return a.time > b.time;});
	return res;
}

QString saves_new_path(const QString& dir, int kind) {
	QDateTime now = QDateTime::currentDateTime();
	QString path;
	// two saves in one millisecond do not share a name
	do {
		path = QDir(dir).filePath(QString("%1-%2.szx").arg(save_prefix[kind], now.toString("yyyyMMdd-HHmmss-zzz")));
		now = now.addMSecs(1);
	} while (QFileInfo::exists(path));
	return path;
}

void saves_remove(const xSave& sv) {
	QFile::remove(sv.path);
	if (!sv.pic.isEmpty()) QFile::remove(sv.pic);
}

void saves_trim(const QString& dir, int kind, int keep) {
	QList<xSave> list = saves_list(dir, kind);
	for (int i = qMax(keep, 0); i < list.size(); i++)
		saves_remove(list.at(i));
}

QString saves_time_text(const QDateTime& t) {
	QDate today = QDate::currentDate();
	QString time = t.toString("HH:mm:ss");
	if (t.date() == today) return "Today " + time;
	if (t.date() == today.addDays(-1)) return "Yesterday " + time;
	return t.toString("yyyy-MM-dd ") + time;
}

QString saves_kind_name(int kind) {
	switch (kind) {
		case SAVE_QUICK: return "Quick";
		case SAVE_EXIT: return "On exit";
		case SAVE_TIMED: return "Auto";
	}
	return QString();
}
