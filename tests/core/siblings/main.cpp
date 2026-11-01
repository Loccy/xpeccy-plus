// siblings-bench: the side/part sets of src/xcore/siblings.cpp over folders of names.
// For each `== folder` of the file given, every name with its prefix and the others of
// its set, or "-".

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "../../../src/xcore/siblings.h"

static void folder(const std::string& title, const std::vector<std::string>& names) {
	if (title.empty()) return;
	std::printf("== %s\n", title.c_str());
	for (int i = 0; i < int(names.size()); i++) {
		std::vector<std::string> others;
		for (int j : sib_group(names, i))
			if (j != i) others.push_back(names[j]);
		std::sort(others.begin(), others.end());
		std::string prefix;
		const bool marked = sib_prefix(names[i], &prefix);
		std::printf("%s [%s]:", names[i].c_str(), marked ? prefix.c_str() : "-");
		if (others.empty()) std::printf(" -");
		else std::printf(" {%s}", sib_value(names[i]).c_str());
		for (const std::string& o : others) std::printf(" | %s", o.c_str());
		std::printf("\n");
		// every sibling starts with the prefix the name gives
		for (const std::string& o : others) {
			std::string low(o);
			for (char& c : low)
				if ((c >= 'A') && (c <= 'Z')) c += 32;
			if (low.compare(0, prefix.size(), prefix)) std::printf("  PREFIX MISS %s\n", o.c_str());
		}
	}
}

int main(int argc, char** argv) {
	if (argc != 2) return 2;
	std::ifstream in(argv[1], std::ios::binary);
	if (!in) return 2;
	std::string line, title;
	std::vector<std::string> names;
	while (std::getline(in, line)) {
		if (!line.empty() && (line.back() == '\r')) line.pop_back();
		if (line.empty() || (line[0] == '#')) continue;
		if (!line.compare(0, 3, "== ")) {
			folder(title, names);
			title = line.substr(3);
			names.clear();
		} else {
			names.push_back(line);
		}
	}
	folder(title, names);
	return 0;
}
