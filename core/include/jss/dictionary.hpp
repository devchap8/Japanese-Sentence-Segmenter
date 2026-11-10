#pragma once

#include <cstddef>
#include <istream>
#include <string>
#include <vector>

#include "jss/trie.hpp"

namespace jss {

// One entry of data/dict.tsv (format in data/README.md). All text is UTF-8.

struct KanjiForm {
    std::string text;
    std::vector<std::string> priority;  // JMdict priority tags, e.g. "ichi1", "nf25"
};

struct Reading {
    std::string text;
    std::vector<std::string> priority;
    // Which kanji forms this reading goes with:
    //   applies_to_all            -> every form in Entry::kanji ("*" in the file)
    //   applies_to not empty      -> only the forms listed
    //   neither                   -> never written with kanji (re_nokanji),
    //                                or the entry has no kanji forms at all
    bool applies_to_all = false;
    std::vector<std::string> applies_to;
    std::vector<int> pitch;  // downstep positions; empty means no data
};

struct Sense {
    std::vector<std::string> pos;      // parts of speech, e.g. "v1", "vt"
    std::vector<std::string> glosses;  // English glosses; may contain commas
};

struct Entry {
    int jmdict_id = 0;  // JMdict ent_seq
    std::vector<KanjiForm> kanji;
    std::vector<Reading> readings;
    std::vector<Sense> senses;
};

// The entry store plus a trie over every kanji form and reading.
// The trie stores indexes into `entries` (0..N-1), not JMdict ids,
// so going from a match to its entry is one array access.
struct Dictionary {
    std::vector<Entry> entries;
    Trie trie;
    std::size_t key_count = 0;  // (key, entry) pairs inserted into the trie
};

// Reads dict.tsv from a stream. Throws std::runtime_error with the line
// number if a line is malformed; a partly loaded dictionary is never returned.
Dictionary load_dictionary(std::istream& in);

// Same, from a file path. Throws std::runtime_error if the file can't be opened.
Dictionary load_dictionary_file(const std::string& path);

}  // namespace jss
