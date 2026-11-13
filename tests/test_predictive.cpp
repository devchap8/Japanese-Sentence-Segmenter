// Tests for predictive (starts-with) search. They expect this addition to
// jss/trie.hpp:
//
//   namespace jss {
//   struct PredictiveMatch {
//       std::u32string key;     // the whole dictionary key, prefix included
//       std::vector<int> ids;   // every id inserted under that key
//   };
//   class Trie {
//   public:
//       // Every key that starts with `prefix` (the prefix itself included, if
//       // it is a key), in code point order, at most `limit` keys.
//       std::vector<PredictiveMatch> predictive_search(const std::u32string& prefix,
//                                                      std::size_t limit) const;
//   };
//   }
//
// Rules the tests check:
//   - Order is code point order of the keys, which is what a pre-order DFS
//     visiting children in their sorted order gives you: a node's own key
//     comes before everything below it.
//   - `limit` counts keys, not ids. A key with three homographs is one result.
//   - limit 0 returns nothing. Pass SIZE_MAX for "no limit".
//   - The empty key is never reported, matching common_prefix_search.
//
// All calls go through predict() below, so a different return type only needs
// changing there.
//
// Run just these tests with: build/jss_tests "[predictive]"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "jss/trie.hpp"
#include "jss/utf8.hpp"

namespace {

using Ids = std::vector<int>;
// (key, sorted ids), in the order the trie returned them.
using Results = std::vector<std::pair<std::u32string, Ids>>;

constexpr std::size_t kNoLimit = std::numeric_limits<std::size_t>::max();

Results predict(const jss::Trie& trie, const std::u32string& prefix,
                std::size_t limit = kNoLimit) {
    Results out;
    for (const auto& match : trie.predictive_search(prefix, limit)) {
        Ids ids = match.ids;
        std::sort(ids.begin(), ids.end());
        out.emplace_back(match.key, std::move(ids));
    }
    return out;
}

// Readable failure messages: Catch2 can't print a u32string on its own.
std::string show(const Results& results) {
    std::string out = "[";
    for (const auto& [key, ids] : results) {
        out += " " + jss::encode_utf8(key) + ":{";
        for (int id : ids) {
            out += " " + std::to_string(id);
        }
        out += " }";
    }
    return out + " ]";
}

#define REQUIRE_RESULTS(actual, expected)        \
    do {                                         \
        const Results actual_ = (actual);        \
        const Results expected_ = (expected);    \
        CAPTURE(show(actual_), show(expected_)); \
        REQUIRE(actual_ == expected_);           \
    } while (false)

// --- Oracle ---------------------------------------------------------------

struct Key {
    std::u32string word;
    int id;
};

// The slow, obviously correct version: check every key, sort, cut.
Results brute_force_predict(const std::vector<Key>& keys, const std::u32string& prefix,
                            std::size_t limit) {
    std::map<std::u32string, Ids> by_key;  // std::map keeps keys in code point order
    for (const Key& key : keys) {
        const bool starts_with = key.word.size() >= prefix.size() &&
                                 key.word.compare(0, prefix.size(), prefix) == 0;
        if (starts_with && !key.word.empty()) {
            by_key[key.word].push_back(key.id);
        }
    }

    Results out;
    for (auto& [word, ids] : by_key) {
        if (out.size() == limit) {
            break;
        }
        std::sort(ids.begin(), ids.end());
        out.emplace_back(word, std::move(ids));
    }
    return out;
}

// Same generator as test_trie.cpp: rng() % n, so a seed fails the same way on
// every standard library.
std::size_t random_below(std::mt19937& rng, std::size_t n) {
    return static_cast<std::size_t>(rng() % static_cast<std::uint32_t>(n));
}

std::u32string random_word(std::mt19937& rng, const std::u32string& alphabet,
                           std::size_t max_length) {
    const std::size_t length = 1 + random_below(rng, max_length);
    std::u32string word;
    for (std::size_t i = 0; i < length; ++i) {
        word.push_back(alphabet[random_below(rng, alphabet.size())]);
    }
    return word;
}

}  // namespace

// --- Basics ---------------------------------------------------------------

TEST_CASE("predictive search on an empty trie finds nothing", "[predictive]") {
    const jss::Trie trie;
    REQUIRE_RESULTS(predict(trie, U"た"), Results{});
    REQUIRE_RESULTS(predict(trie, U""), Results{});
}

TEST_CASE("predictive search lists words that start with the input", "[predictive]") {
    jss::Trie trie;
    trie.insert(U"たべる", 1);
    trie.insert(U"たべもの", 2);
    trie.insert(U"たべものや", 3);
    trie.insert(U"たま", 4);
    trie.insert(U"べる", 5);

    // も (U+3082) sorts before る (U+308B).
    REQUIRE_RESULTS(predict(trie, U"たべ"),
                    (Results{{U"たべもの", {2}}, {U"たべものや", {3}}, {U"たべる", {1}}}));
    REQUIRE_RESULTS(predict(trie, U"たま"), (Results{{U"たま", {4}}}));
}

TEST_CASE("predictive search includes the input itself when it is a word", "[predictive]") {
    jss::Trie trie;
    trie.insert(U"日本語", 2);
    trie.insert(U"日本", 1);
    trie.insert(U"日本人", 3);

    REQUIRE_RESULTS(predict(trie, U"日本"),
                    (Results{{U"日本", {1}}, {U"日本人", {3}}, {U"日本語", {2}}}));
}

TEST_CASE("predictive search with no matching path returns nothing", "[predictive]") {
    jss::Trie trie;
    trie.insert(U"たべる", 1);

    REQUIRE_RESULTS(predict(trie, U"のむ"), Results{});
    REQUIRE_RESULTS(predict(trie, U"たま"), Results{});      // fails partway down
    REQUIRE_RESULTS(predict(trie, U"たべるな"), Results{});  // longer than any key
}

TEST_CASE("predictive search keeps every homograph under one key", "[predictive]") {
    jss::Trie trie;
    trie.insert(U"はし", 30);
    trie.insert(U"はし", 10);
    trie.insert(U"はしる", 40);
    trie.insert(U"はし", 20);

    REQUIRE_RESULTS(predict(trie, U"は"), (Results{{U"はし", {10, 20, 30}}, {U"はしる", {40}}}));
}

TEST_CASE("predictive search with an empty prefix lists every key", "[predictive]") {
    jss::Trie trie;
    trie.insert(U"b", 2);
    trie.insert(U"", 9);  // never reported
    trie.insert(U"a", 1);
    trie.insert(U"ab", 3);

    REQUIRE_RESULTS(predict(trie, U""), (Results{{U"a", {1}}, {U"ab", {3}}, {U"b", {2}}}));
}

// --- Limit ----------------------------------------------------------------

TEST_CASE("the limit keeps the first keys in order", "[predictive]") {
    jss::Trie trie;
    trie.insert(U"ad", 4);
    trie.insert(U"ab", 2);
    trie.insert(U"a", 1);
    trie.insert(U"ac", 3);
    trie.insert(U"abz", 5);

    REQUIRE_RESULTS(predict(trie, U"a", 0), Results{});
    REQUIRE_RESULTS(predict(trie, U"a", 1), (Results{{U"a", {1}}}));
    REQUIRE_RESULTS(predict(trie, U"a", 3), (Results{{U"a", {1}}, {U"ab", {2}}, {U"abz", {5}}}));
    REQUIRE_RESULTS(predict(trie, U"a", 5), predict(trie, U"a"));
    REQUIRE_RESULTS(predict(trie, U"a", 100), predict(trie, U"a"));
    REQUIRE(predict(trie, U"a").size() == 5);
}

TEST_CASE("the limit counts keys, not ids", "[predictive]") {
    jss::Trie trie;
    trie.insert(U"はし", 1);
    trie.insert(U"はし", 2);
    trie.insert(U"はし", 3);
    trie.insert(U"はしる", 4);

    REQUIRE_RESULTS(predict(trie, U"は", 1), (Results{{U"はし", {1, 2, 3}}}));
    REQUIRE_RESULTS(predict(trie, U"は", 2), (Results{{U"はし", {1, 2, 3}}, {U"はしる", {4}}}));
}

// --- Edge cases -----------------------------------------------------------

TEST_CASE("predictive search orders by code point, not by UTF-16", "[predictive]") {
    // U+FFFF < U+20BB7 as code points, but in UTF-16 𠮷 starts with 0xD842,
    // which sorts before 0xFFFF. A u32string compare gets this right.
    jss::Trie trie;
    trie.insert(U"x\U00020BB7", 2);
    trie.insert(U"x￿", 1);
    trie.insert(U"x猫", 0);

    REQUIRE_RESULTS(predict(trie, U"x"),
                    (Results{{U"x猫", {0}}, {U"x￿", {1}}, {U"x\U00020BB7", {2}}}));
}

TEST_CASE("predictive search under a very deep subtree", "[predictive]") {
    // A recursive DFS goes 10000 calls deep here, which can overflow the stack
    // in a Debug build. An explicit stack doesn't care.
    const std::u32string word(10000, U'あ');
    jss::Trie trie;
    trie.insert(word, 1);
    trie.insert(word.substr(0, 5000), 2);

    REQUIRE_RESULTS(predict(trie, U"あ"), (Results{{word.substr(0, 5000), {2}}, {word, {1}}}));
}

TEST_CASE("predictive search doesn't change what find returns", "[predictive]") {
    jss::Trie trie;
    trie.insert(U"たべる", 1);
    trie.insert(U"たべもの", 2);

    predict(trie, U"た");
    REQUIRE(trie.find(U"たべる") == Ids{1});
    REQUIRE(trie.find(U"たべもの") == Ids{2});
}

// --- Randomized oracle ----------------------------------------------------

TEST_CASE("predictive search agrees with a brute-force scan on random dictionaries",
          "[predictive][oracle]") {
    const std::size_t dictionary_size = GENERATE(1, 2, 10, 100, 1000);
    const std::u32string alphabet = GENERATE(as<std::u32string>{},
                                             U"ab",                            // heavy sharing
                                             U"あいうえお",                     // kana
                                             U"a猫\U00020BB7\U0001F600\U0010FFFF");  // all widths
    const std::size_t limit = GENERATE(as<std::size_t>{}, 1, 7, kNoLimit);
    const std::uint32_t seed = GENERATE(1u, 2u, 3u);

    CAPTURE(dictionary_size, limit, seed, jss::encode_utf8(alphabet));

    std::mt19937 rng(seed);
    std::vector<Key> keys;
    for (std::size_t i = 0; i < dictionary_size; ++i) {
        keys.push_back({random_word(rng, alphabet, 8), static_cast<int>(i)});
    }

    jss::Trie trie;
    for (const Key& key : keys) {
        trie.insert(key.word, key.id);
    }

    std::size_t total_results = 0;
    auto check = [&](const std::u32string& prefix) {
        const Results expected = brute_force_predict(keys, prefix, limit);
        total_results += expected.size();
        CAPTURE(jss::encode_utf8(prefix));
        REQUIRE_RESULTS(predict(trie, prefix, limit), expected);
    };

    SECTION("prefixes of real keys") {
        for (int i = 0; i < 300; ++i) {
            const std::u32string& word = keys[random_below(rng, keys.size())].word;
            check(word.substr(0, random_below(rng, word.size() + 1)));
        }
        // Guards against a generator bug turning this test into nothing.
        REQUIRE(total_results > 0);
    }

    SECTION("random prefixes") {
        for (int i = 0; i < 300; ++i) {
            check(random_word(rng, alphabet, 4));
        }
    }

    SECTION("the empty prefix") {
        check(U"");
        REQUIRE(total_results > 0);
    }
}
