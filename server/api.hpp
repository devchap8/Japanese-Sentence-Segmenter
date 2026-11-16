#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "jss/dictionary.hpp"

namespace jss {

// The JSON answers of jss_server, kept apart from the HTTP code so tests can
// call them directly.

enum class SearchMode {
    prefix,      // every dictionary word the query starts with, longest first
    predictive,  // every dictionary word that starts with the query, most common first
};

constexpr std::size_t kMaxQueryLength = 64;  // code points; longer queries are cut
constexpr std::size_t kDefaultLimit = 50;
constexpr std::size_t kMaxLimit = 500;

// One entry: id, common flag, kanji forms, readings (with pitch), senses.
nlohmann::json entry_json(const Entry& entry);

// GET /api/search. The response:
//
//   query      the query as received (cleaned up: trimmed, invalid UTF-8 replaced, cut to kMaxQueryLength)
//   searched   what was searched: the query, or its hiragana form if the query
//              had katakana and found nothing as typed
//   mode       "prefix" or "predictive"
//   truncated  true if the query was cut to kMaxQueryLength
//   total      matches found before the limit was applied
//   path       prefix mode: the trie nodes the walk visited, one per character,
//              [{"char": "日", "terminal": false}, ...]; stops where the walk stopped
//   walk_stopped  prefix mode: true if the walk ended before the end of the query
//   matches    [{"key": "日本語", "length": 3, "entries": [...]}, ...]
//
// `limit` caps the number of matches (keys) in predictive mode; prefix mode
// never has more matches than the query has characters.
nlohmann::json search_json(const Dictionary& dict, std::string_view query, SearchMode mode,
                           std::size_t limit = kDefaultLimit);

// Katakana (U+30A1..U+30F6) to hiragana; everything else unchanged.
std::u32string katakana_to_hiragana(std::u32string text);

}  // namespace jss
