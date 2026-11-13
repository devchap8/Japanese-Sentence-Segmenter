#include "jss/ranking.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "jss/utf8.hpp"

namespace jss {

namespace {

// "nf01".."nf48" -> 1..48; anything else -> 0.
int nf_number(const std::string& tag) {
    if (tag.size() != 4 || tag[0] != 'n' || tag[1] != 'f') return 0;
    if (tag[2] < '0' || tag[2] > '9' || tag[3] < '0' || tag[3] > '9') return 0;
    return (tag[2] - '0') * 10 + (tag[3] - '0');
}

bool is_common_tag(const std::string& tag) {
    return tag == "news1" || tag == "ichi1" || tag == "spec1" || tag == "spec2" ||
           tag == "gai1";
}

}  // namespace

int priority_score(const std::vector<std::string>& tags) {
    if (tags.empty()) return kUntaggedScore;

    bool common = false;
    int nf = 49;  // after every real band
    for (const std::string& tag : tags) {
        if (is_common_tag(tag)) common = true;
        const int n = nf_number(tag);
        if (n > 0 && n < nf) nf = n;
    }
    return (common ? 0 : 100) + nf;
}

int form_score(const Entry& entry, std::string_view key) {
    int best = kUntaggedScore;
    for (const KanjiForm& k : entry.kanji) {
        if (k.text == key) best = std::min(best, priority_score(k.priority));
    }
    for (const Reading& r : entry.readings) {
        if (r.text == key) best = std::min(best, priority_score(r.priority));
    }
    return best;
}

void rank_ids(const Dictionary& dict, std::string_view key, std::vector<int>& ids) {
    // Score each id once, not once per comparison.
    std::vector<std::pair<int, int>> scored;  // (score, id)
    scored.reserve(ids.size());
    for (int id : ids) {
        scored.emplace_back(form_score(dict.entries[static_cast<std::size_t>(id)], key), id);
    }
    std::stable_sort(scored.begin(), scored.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    for (std::size_t i = 0; i < ids.size(); ++i) ids[i] = scored[i].second;
}

void rank_prefix_matches(const Dictionary& dict, const std::u32string& text,
                         std::size_t start, std::vector<PrefixMatch>& matches) {
    std::sort(matches.begin(), matches.end(),
              [](const PrefixMatch& a, const PrefixMatch& b) { return a.length > b.length; });
    for (PrefixMatch& m : matches) {
        const std::string key = encode_utf8(std::u32string_view(text).substr(start, m.length));
        rank_ids(dict, key, m.ids);
    }
}

void rank_predictive_matches(const Dictionary& dict, std::vector<PredictiveMatch>& matches) {
    struct Ranked {
        int score;
        PredictiveMatch match;
    };
    std::vector<Ranked> ranked;
    ranked.reserve(matches.size());
    for (PredictiveMatch& m : matches) {
        const std::string key = encode_utf8(m.key);
        rank_ids(dict, key, m.ids);
        const int best = m.ids.empty()
                             ? kUntaggedScore
                             : form_score(dict.entries[static_cast<std::size_t>(m.ids[0])], key);
        ranked.push_back({best, std::move(m)});
    }

    std::sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
        const std::size_t a_length = a.match.key.size();
        const std::size_t b_length = b.match.key.size();
        return std::tie(a.score, a_length, a.match.key) <
               std::tie(b.score, b_length, b.match.key);
    });

    for (std::size_t i = 0; i < matches.size(); ++i) matches[i] = std::move(ranked[i].match);
}

}  // namespace jss
