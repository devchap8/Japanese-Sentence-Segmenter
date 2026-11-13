#include "jss/baselines.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "jss/utf8.hpp"

namespace jss {

namespace {

bool starts_with(const std::u32string& s, const std::u32string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

}  // namespace

// --- HashIndex ------------------------------------------------------------

void HashIndex::insert(const std::u32string& word, int id) {
    map_[word].push_back(id);
    longest_key_ = std::max(longest_key_, word.size());
}

std::vector<int> HashIndex::find(const std::u32string& word) const {
    const auto it = map_.find(word);
    return it == map_.end() ? std::vector<int>{} : it->second;
}

std::vector<PrefixMatch> HashIndex::common_prefix_search(const std::u32string& text,
                                                         std::size_t start) const {
    std::vector<PrefixMatch> matches;
    if (start >= text.size()) return matches;

    const std::size_t max_length = std::min(longest_key_, text.size() - start);
    std::u32string candidate;
    for (std::size_t length = 1; length <= max_length; ++length) {
        candidate.push_back(text[start + length - 1]);
        const auto it = map_.find(candidate);
        if (it != map_.end()) matches.push_back({length, it->second});
    }
    return matches;
}

std::vector<PredictiveMatch> HashIndex::predictive_search(const std::u32string& prefix,
                                                          std::size_t limit) const {
    std::vector<PredictiveMatch> results;
    if (limit == 0) return results;
    for (const auto& [key, ids] : map_) {
        if (!key.empty() && starts_with(key, prefix)) results.push_back({key, ids});
    }
    // A hash map has no order, so sort the whole match set before cutting it.
    std::sort(results.begin(), results.end(),
              [](const PredictiveMatch& a, const PredictiveMatch& b) { return a.key < b.key; });
    if (results.size() > limit) results.resize(limit);
    return results;
}

// --- SortedArrayIndex -----------------------------------------------------

void SortedArrayIndex::insert(const std::u32string& word, int id) {
    if (built_) throw std::logic_error("SortedArrayIndex::insert after build()");
    pending_.emplace_back(word, id);
}

void SortedArrayIndex::build() {
    if (built_) return;
    built_ = true;
    // Stable, so ids under one key stay in insertion order, as in the trie.
    std::stable_sort(pending_.begin(), pending_.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& [key, id] : pending_) {
        if (items_.empty() || items_.back().key != key) items_.push_back({key, {}});
        items_.back().ids.push_back(id);
    }
    pending_.clear();
    pending_.shrink_to_fit();
}

std::vector<int> SortedArrayIndex::find(const std::u32string& word) const {
    const auto it = std::lower_bound(items_.begin(), items_.end(), word,
                                     [](const Item& item, const std::u32string& w) {
                                         return item.key < w;
                                     });
    return it != items_.end() && it->key == word ? it->ids : std::vector<int>{};
}

std::vector<PrefixMatch> SortedArrayIndex::common_prefix_search(const std::u32string& text,
                                                                std::size_t start) const {
    std::vector<PrefixMatch> matches;
    // [lo, hi): the run of keys that start with text[start, start + depth).
    auto lo = items_.begin();
    auto hi = items_.end();

    for (std::size_t depth = 0; start + depth < text.size() && lo != hi; ++depth) {
        // In the run, a key exactly depth characters long sorts first (it is a
        // prefix of the others). It was reported last step; step past it, so
        // every key left has a character at position depth.
        if (lo->key.size() == depth) ++lo;

        const char32_t c = text[start + depth];
        lo = std::lower_bound(lo, hi, c, [depth](const Item& item, char32_t ch) {
            return item.key[depth] < ch;
        });
        hi = std::upper_bound(lo, hi, c, [depth](char32_t ch, const Item& item) {
            return ch < item.key[depth];
        });

        if (lo != hi && lo->key.size() == depth + 1) {
            matches.push_back({depth + 1, lo->ids});
        }
    }
    return matches;
}

std::vector<PredictiveMatch> SortedArrayIndex::predictive_search(const std::u32string& prefix,
                                                                 std::size_t limit) const {
    std::vector<PredictiveMatch> results;
    auto it = std::lower_bound(items_.begin(), items_.end(), prefix,
                               [](const Item& item, const std::u32string& p) {
                                   return item.key < p;
                               });
    for (; it != items_.end() && results.size() < limit && starts_with(it->key, prefix); ++it) {
        if (!it->key.empty()) results.push_back({it->key, it->ids});
    }
    return results;
}

// --- dictionary_keys ------------------------------------------------------

std::vector<std::pair<std::u32string, int>> dictionary_keys(const Dictionary& dict) {
    std::vector<std::pair<std::u32string, int>> keys;
    keys.reserve(dict.key_count);
    for (std::size_t index = 0; index < dict.entries.size(); ++index) {
        const Entry& entry = dict.entries[index];
        std::vector<const std::string*> spellings;
        for (const KanjiForm& k : entry.kanji) spellings.push_back(&k.text);
        for (const Reading& r : entry.readings) spellings.push_back(&r.text);

        std::vector<const std::string*> done;
        for (const std::string* s : spellings) {
            const bool seen = std::any_of(done.begin(), done.end(),
                                          [s](const std::string* d) { return *d == *s; });
            if (!seen) {
                keys.emplace_back(decode_utf8(*s), static_cast<int>(index));
                done.push_back(s);
            }
        }
    }
    return keys;
}

}  // namespace jss
