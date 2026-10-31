#pragma once

// Sibling images: the other sides, tapes, disks or parts of a set, found by name among
// the files of one folder. A name marks its place in a set by a side token:
//   - a bracket of Side/Tape/Disk/Disc/Part words, each with A-D or 1-99 and maybe
//     "of N": "(Side B)", "(Disk 2 of 3)", "(Tape 1 of 2 Side A)";
//   - such a word, any case, then a space, _, - or nothing, then the value: "Side 1",
//     "side_b", "PART 2", "Part2"; or capitalized after a small letter: "RexSideA";
//   - with none of those, a lone A-D ending the title (the name up to its first
//     bracket) after a space, _ or -: "Game A", "VIXEN_B".
// A digit alone never marks one: "Dizzy 2" and "Dizzy 3" are two games, nor does a word
// in a [flag]: "[aka Back to the Future Part 2]". The last token counts. Names are a
// set when they are the same with their tokens cut out (any case, the "of N" kept) and
// the tokens differ. Names are UTF-8; an archive's own extension (.zip .gz .7z) is
// looked through.

#include <string>
#include <vector>

// The start every sibling's name has (lower case): the name up to its token or its
// first [flag]. False when the name marks no place in a set.
bool sib_prefix(const std::string& name, std::string* prefix);

// The set names[cur] belongs to, as indexes into names in their order, cur among them;
// empty when there is none. Steps, the first that finds a set wins: the same extension,
// packed alike, with every [flag]; the same without [flags]; then any extension, packed
// or not - the caller gives only names of the kind it takes.
std::vector<int> sib_group(const std::vector<std::string>& names, int cur);
