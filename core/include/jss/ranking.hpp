#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "jss/dictionary.hpp"
#include "jss/trie.hpp"

namespace jss {

// Ranking by JMdict priority tags. Lower scores are more common.
//
//   common word (news1, ichi1, spec1, spec2, gai1) with nfXX   ->   1..48  (XX)
//   common word without nfXX                                   ->  49
//   only other tags (news2, ichi2, gai2), with nfXX            -> 101..148
//   only other tags, without nfXX                              -> 149
//   no tags at all                                             -> 200
//
// "Common" is JMdict's own definition. nfXX is the word's frequency band in
// newspaper text (nf01 = the 500 most frequent words, nf02 the next 500...),
// so it orders words inside each group.
constexpr int kUntaggedScore = 200;

int priority_score(const std::vector<std::string>& tags);

// Best score among the forms of `entry` spelled exactly `key` (kanji forms
// and readings both count). kUntaggedScore if no form is spelled `key`.
int form_score(const Entry& entry, std::string_view key);

// Sorts entry indexes that share one key, most common first.
// Ties keep their current order (file order, as the trie returns them).
void rank_ids(const Dictionary& dict, std::string_view key, std::vector<int>& ids);

// Common-prefix matches of `text` at `start`: longest first, then the
// entries of each match by rank_ids.
void rank_prefix_matches(const Dictionary& dict, const std::u32string& text,
                         std::size_t start, std::vector<PrefixMatch>& matches);

// Predictive matches: keys by their best entry's score, then shorter keys
// first, then code point order. The entries of each key by rank_ids.
//
// This only reorders what it is given. To get the top k by rank, search with a
// limit larger than k (or none), rank, then cut to k: predictive_search's own
// limit keeps the first k in code point order, not the most common k.
void rank_predictive_matches(const Dictionary& dict, std::vector<PredictiveMatch>& matches);

}  // namespace jss
