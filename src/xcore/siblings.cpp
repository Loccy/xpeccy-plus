#include "siblings.h"

#include <algorithm>
#include <set>

// ASCII only: the words and values are, and the rest of a name only has to compare
static bool is_alpha(char c) {return ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z'));}
static bool is_digit(char c) {return (c >= '0') && (c <= '9');}
static bool is_alnum(char c) {return is_alpha(c) || is_digit(c);}
static bool is_lower(char c) {return (c >= 'a') && (c <= 'z');}
static bool is_upper(char c) {return (c >= 'A') && (c <= 'Z');}
static char lower(char c) {return is_upper(c) ? char(c + 32) : c;}
static bool is_sep(char c) {return (c == ' ') || (c == '_') || (c == '-');}

static std::string lowered(const std::string& s) {
	std::string res(s);
	for (char& c : res) c = lower(c);
	return res;
}

static bool ends_with(const std::string& s, const char* tail) {
	const std::string t(tail);
	return (s.size() > t.size()) && (lowered(s.substr(s.size() - t.size())) == t);
}

static const char* sib_words[] = {"side", "tape", "disk", "disc", "part"};

static bool word_at(const std::string& s, size_t i) {
	if (i + 4 > s.size()) return false;
	const std::string w = lowered(s.substr(i, 4));
	for (const char* word : sib_words)
		if (w == word) return true;
	return false;
}

typedef struct {
	size_t start;
	size_t end;
	std::string value;	// "side a", "tape 1 side b"
	std::string total;	// the "of N" numbers: "2", "2/3"
} sibToken;

// One word with its value at s[i], glued on when camel; 0 when there is none, else
// where it ends
static size_t item_at(const std::string& s, size_t i, bool camel, sibToken* tok) {
	if (!word_at(s, i)) return 0;
	size_t j = i + 4;
	if (!camel)
		while ((j < s.size()) && is_sep(s[j])) j++;
	if (j >= s.size()) return 0;
	std::string val;
	if (is_digit(s[j]) && (s[j] != '0')) {
		val += s[j++];
		if ((j < s.size()) && is_digit(s[j])) val += s[j++];
	} else if ((lower(s[j]) >= 'a') && (lower(s[j]) <= 'd')) {
		// a letter glued on is a capital ("SideA") or ends the name ("ianna-sidea"):
		// "Taped Off" and "Two-Sided Game" are words
		const bool glued = (j == i + 4);
		const bool ends = (j + 1 == s.size()) || (s[j + 1] == '(') || (s[j + 1] == '[') || (s[j + 1] == ')');
		if (glued && !(is_upper(s[j]) && is_lower(s[i + 3])) && !ends) return 0;
		val += lower(s[j++]);
	} else {
		return 0;
	}
	if ((j < s.size()) && is_alnum(s[j])) return 0;
	std::string total;
	if (!camel) {
		size_t k = j;
		while ((k < s.size()) && (s[k] == ' ')) k++;
		if ((k + 1 < s.size()) && (lower(s[k]) == 'o') && (lower(s[k + 1]) == 'f')) {
			k += 2;
			while ((k < s.size()) && (s[k] == ' ')) k++;
			const size_t d = k;
			while ((k < s.size()) && is_digit(s[k]) && (k - d < 2)) k++;
			if ((k > d) && (s[d] != '0') && !((k < s.size()) && is_alnum(s[k]))) {
				total = s.substr(d, k - d);
				j = k;
			}
		}
	}
	if (!tok->value.empty()) tok->value += ' ';
	tok->value += lowered(s.substr(i, 4)) + ' ' + val;
	if (!total.empty()) {
		if (!tok->total.empty()) tok->total += '/';
		tok->total += total;
	}
	return j;
}

// a bracket of words at s[i] = '('
static bool bracket_at(const std::string& s, size_t i, sibToken* tok) {
	sibToken t = {i, 0, std::string(), std::string()};
	size_t j = i + 1;
	for (;;) {
		while ((j < s.size()) && (s[j] == ' ')) j++;
		const size_t e = item_at(s, j, false, &t);
		if (!e) break;
		j = e;
	}
	if (t.value.empty() || (j >= s.size()) || (s[j] != ')')) return false;
	t.end = j + 1;
	*tok = t;
	return true;
}

// where the title ends: the first bracket, the spaces before it left out
static size_t title_end(const std::string& s) {
	size_t te = s.size();
	for (size_t i = 1; i < s.size(); i++) {
		if ((s[i] == '(') || (s[i] == '[')) {
			te = i;
			break;
		}
	}
	while ((te > 0) && (s[te - 1] == ' ')) te--;
	return te;
}

// the last token of a stem
static bool find_token(const std::string& s, sibToken* tok) {
	bool found = false;
	int flag = 0;
	for (size_t i = 0; i < s.size(); i++) {
		// a [flag] is a note on the file, "[aka ... Part 2]", never its place in the set
		if (s[i] == '[') flag++;
		else if ((s[i] == ']') && flag) flag--;
		if (flag) continue;
		sibToken t = {i, 0, std::string(), std::string()};
		if (s[i] == '(') {
			if (!bracket_at(s, i, &t)) continue;
		} else if ((i == 0) || !is_alpha(s[i - 1])) {
			t.end = item_at(s, i, false, &t);
		} else if (is_lower(s[i - 1]) && is_upper(s[i]) && (i + 4 <= s.size())
				&& is_lower(s[i + 1]) && is_lower(s[i + 2]) && is_lower(s[i + 3])) {
			t.end = item_at(s, i, true, &t);
		}
		if (!t.end) continue;
		// the one ending last; a bracket wins over a word inside it
		if (!found || (t.end > tok->end)) *tok = t;
		found = true;
	}
	if (found) return true;
	// a lone letter ending the title
	const size_t te = title_end(s);
	if ((te < 3) || !is_sep(s[te - 2])) return false;
	const char c = lower(s[te - 1]);
	if ((c < 'a') || (c > 'd')) return false;
	*tok = {te - 1, te, std::string(1, c), std::string()};
	return true;
}

// the title ends in word after a separator: where the separators before it start
static size_t title_tail(const std::string& s, size_t te, const char* word) {
	const std::string w(word);
	if ((te <= w.size()) || (lowered(s.substr(te - w.size(), w.size())) != w)) return 0;
	size_t i = te - w.size();
	if (!is_sep(s[i - 1])) return 0;
	while ((i > 0) && is_sep(s[i - 1])) i--;
	return i;
}

// A game's level tape beside it, as WoS names some: "Narc - 128k" + "Narc - 128k -
// Levels", "Popeye 3 - ... - Game" + "... - Levels", "Bloodwych - The Game" +
// "Bloodwych - Level Data". Only for a name no side token marks: the level tape is
// "levels", the other "game", with the title cut there.
static void level_token(const std::string& s, sibToken* tok) {
	const size_t te = title_end(s);
	static const char* level_words[] = {"levels", "level data"};
	static const char* game_words[] = {"the game", "game"};
	for (const char* w : level_words) {
		if (const size_t i = title_tail(s, te, w)) {
			*tok = {i, te, "levels", std::string()};
			return;
		}
	}
	for (const char* w : game_words) {
		if (const size_t i = title_tail(s, te, w)) {
			*tok = {i, te, "game", std::string()};
			return;
		}
	}
	*tok = {te, te, "game", std::string()};
}

typedef struct {
	std::string key;	// the stem without its token, lower case
	std::string value;
	std::string ext;
	bool packed;
} sibName;

// a stem without its [flags], spaces run together
static std::string no_flags(const std::string& s) {
	std::string res;
	int depth = 0;
	for (char c : s) {
		if (c == '[') depth++;
		else if ((c == ']') && depth) depth--;
		else if (!depth && !((c == ' ') && (res.empty() || (res.back() == ' ')))) res += c;
	}
	while (!res.empty() && (res.back() == ' ')) res.pop_back();
	return res;
}

static void split_name(const std::string& name, std::string* stem, std::string* ext, bool* packed) {
	std::string s(name);
	*packed = false;
	for (const char* arc : {".zip", ".gz", ".7z"}) {
		if (ends_with(s, arc)) {
			s.resize(s.size() - std::string(arc).size());
			*packed = true;
			break;
		}
	}
	const size_t dot = s.rfind('.');
	*ext = (dot == std::string::npos) ? std::string() : lowered(s.substr(dot + 1));
	*stem = (dot == std::string::npos) ? s : s.substr(0, dot);
}

// The token a stem has: a side token, else the game/levels pair's - level says so, and
// its key is kept apart, so that it never pairs with a side token
static void stem_token(const std::string& stem, sibToken* tok, bool* level) {
	*level = !find_token(stem, tok);
	if (*level) level_token(stem, tok);
}

static void parse_name(const std::string& name, bool flags, sibName* res) {
	std::string stem;
	split_name(name, &stem, &res->ext, &res->packed);
	if (!flags) stem = no_flags(stem);
	sibToken tok;
	bool level;
	stem_token(stem, &tok, &level);
	res->key = std::string(level ? "\x02" : "") + lowered(stem.substr(0, tok.start)) + '\x01' + tok.total + '\x01'
		+ lowered(stem.substr(tok.end));
	res->value = tok.value;
}

bool sib_prefix(const std::string& name, std::string* prefix) {
	std::string stem, ext;
	bool packed;
	split_name(name, &stem, &ext, &packed);
	sibToken tok;
	bool level;
	stem_token(stem, &tok, &level);
	size_t end = std::min(tok.start, stem.find('['));
	// a sibling without the flags may have one space less
	while ((end > 0) && (stem[end - 1] == ' ')) end--;
	*prefix = lowered(stem.substr(0, end));
	return !prefix->empty();
}

std::string sib_value(const std::string& name) {
	sibName me;
	parse_name(name, true, &me);
	return me.value;
}

std::vector<int> sib_group(const std::vector<std::string>& names, int cur) {
	for (int step = 0; step < 3; step++) {
		sibName me;
		parse_name(names[cur], step == 0, &me);
		std::vector<int> grp;
		std::set<std::string> values;
		for (int i = 0; i < int(names.size()); i++) {
			sibName it;
			parse_name(names[i], step == 0, &it);
			if (it.key != me.key) continue;
			if ((step < 2) && ((it.ext != me.ext) || (it.packed != me.packed))) continue;
			grp.push_back(i);
			values.insert(it.value);
		}
		if (values.size() > 1) return grp;
	}
	return std::vector<int>();
}
