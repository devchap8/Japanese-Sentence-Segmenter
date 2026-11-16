#include "api.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "jss/ranking.hpp"
#include "jss/trie.hpp"
#include "jss/utf8.hpp"

namespace jss {

namespace {

using nlohmann::json;

constexpr int kCommonScoreLimit = 100;  // scores below this are JMdict "common" (see ranking.hpp)

bool is_space(char32_t c) {
    return c == U' ' || c == U'\t' || c == U'\n' || c == U'\r' || c == U'　';
}

std::u32string trim(const std::u32string& text) {
    std::size_t begin = 0, end = text.size();
    while (begin < end && is_space(text[begin])) ++begin;
    while (end > begin && is_space(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

json entries_json(const Dictionary& dict, const std::vector<int>& ids) {
    json out = json::array();
    for (int id : ids) out.push_back(entry_json(dict.entries[static_cast<std::size_t>(id)]));
    return out;
}

// The nodes the trie walk visits for `text`. A node exists for a prefix exactly
// when some key starts with it (every node lies on the path of an inserted
// key), so predictive_search(prefix, 1) tells whether the walk gets that far.
json walk_path(const Dictionary& dict, const std::u32string& text,
               const std::vector<PrefixMatch>& matches, bool& stopped) {
    json path = json::array();
    stopped = false;
    for (std::size_t depth = 1; depth <= text.size(); ++depth) {
        const std::u32string prefix = text.substr(0, depth);
        if (dict.trie.predictive_search(prefix, 1).empty()) {
            stopped = true;
            break;
        }
        const bool terminal = std::any_of(matches.begin(), matches.end(),
                                          [depth](const PrefixMatch& m) { return m.length == depth; });
        path.push_back({{"char", encode_utf8(text.substr(depth - 1, 1))}, {"terminal", terminal}});
    }
    return path;
}

}  // namespace

std::u32string katakana_to_hiragana(std::u32string text) {
    for (char32_t& c : text) {
        if (c >= U'ァ' && c <= U'ヶ') c -= 0x60;
    }
    return text;
}

json entry_json(const Entry& entry) {
    int best = kUntaggedScore;
    json kanji = json::array();
    for (const KanjiForm& k : entry.kanji) {
        best = std::min(best, priority_score(k.priority));
        kanji.push_back({{"text", k.text}, {"priority", k.priority}});
    }
    json readings = json::array();
    for (const Reading& r : entry.readings) {
        best = std::min(best, priority_score(r.priority));
        readings.push_back({{"text", r.text},
                            {"priority", r.priority},
                            {"applies_to_all", r.applies_to_all},
                            {"applies_to", r.applies_to},
                            {"pitch", r.pitch}});
    }
    json senses = json::array();
    for (const Sense& s : entry.senses) senses.push_back({{"pos", s.pos}, {"glosses", s.glosses}});

    return {{"id", entry.jmdict_id},
            {"common", best < kCommonScoreLimit},
            {"kanji", std::move(kanji)},
            {"readings", std::move(readings)},
            {"senses", std::move(senses)}};
}

json search_json(const Dictionary& dict, std::string_view query, SearchMode mode, std::size_t limit) {
    std::u32string text = trim(decode_utf8(query));
    const bool truncated = text.size() > kMaxQueryLength;
    if (truncated) text.resize(kMaxQueryLength);
    limit = std::clamp<std::size_t>(limit, 1, kMaxLimit);

    json out = {{"query", encode_utf8(text)},
                {"mode", mode == SearchMode::prefix ? "prefix" : "predictive"},
                {"truncated", truncated}};
    json matches = json::array();

    if (mode == SearchMode::prefix) {
        std::u32string searched = text;
        std::vector<PrefixMatch> found = dict.trie.common_prefix_search(searched, 0);
        if (found.empty()) {
            const std::u32string hiragana = katakana_to_hiragana(text);
            if (hiragana != text) {
                std::vector<PrefixMatch> retry = dict.trie.common_prefix_search(hiragana, 0);
                if (!retry.empty()) {
                    searched = hiragana;
                    found = std::move(retry);
                }
            }
        }
        rank_prefix_matches(dict, searched, 0, found);

        bool stopped = false;
        out["path"] = walk_path(dict, searched, found, stopped);
        out["walk_stopped"] = stopped;
        for (const PrefixMatch& m : found) {
            matches.push_back({{"key", encode_utf8(searched.substr(0, m.length))},
                               {"length", m.length},
                               {"entries", entries_json(dict, m.ids)}});
        }
        out["searched"] = encode_utf8(searched);
        out["total"] = found.size();
    } else {
        // Rank everything under the prefix, then cut: predictive_search's own
        // limit would keep the first keys in code point order, not the most common.
        constexpr std::size_t kAll = std::numeric_limits<std::size_t>::max();
        std::u32string searched = text;
        std::vector<PredictiveMatch> found;
        if (!text.empty()) {
            found = dict.trie.predictive_search(searched, kAll);
            if (found.empty()) {
                const std::u32string hiragana = katakana_to_hiragana(text);
                if (hiragana != text) {
                    found = dict.trie.predictive_search(hiragana, kAll);
                    if (!found.empty()) searched = hiragana;
                }
            }
        }
        rank_predictive_matches(dict, found);
        out["searched"] = encode_utf8(searched);
        out["total"] = found.size();
        if (found.size() > limit) found.resize(limit);
        for (const PredictiveMatch& m : found) {
            matches.push_back({{"key", encode_utf8(m.key)},
                               {"length", m.key.size()},
                               {"entries", entries_json(dict, m.ids)}});
        }
    }

    out["matches"] = std::move(matches);
    return out;
}

}  // namespace jss
