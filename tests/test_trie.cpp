// Tests for the pointer trie. They expect this interface in jss/trie.hpp:
//
//   namespace jss {
//   struct PrefixMatch {
//       std::size_t length;     // code points matched, starting at `start`
//       std::vector<int> ids;   // every id inserted under that key
//   };
//   class Trie {
//   public:
//       Trie();
//       void insert(const std::u32string& word, int id);
//       std::vector<int> find(const std::u32string& word) const;
//       // One PrefixMatch per dictionary word starting at `start`, shortest first.
//       std::vector<PrefixMatch> common_prefix_search(const std::u32string& text,
//                                                     std::size_t start) const;
//   };
//   }
//
// std::u32string_view parameters work too. All calls to find and
// common_prefix_search go through ids_of and matches_at below, so a different
// return type only needs changing there.
//
// Run just these tests with: build/jss_tests "[trie]"
#define CATCH_CONFIG_ENABLE_PAIR_STRINGMAKER

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "jss/trie.hpp"
#include "jss/utf8.hpp"

namespace {

// (length, sorted ids), shortest first.
using Matches = std::vector<std::pair<std::size_t, std::vector<int>>>;

// Ids may come back in any order, so tests compare them sorted.
std::vector<int> ids_of(const jss::Trie& trie, const std::u32string& word) {
    std::vector<int> ids = trie.find(word);
    std::sort(ids.begin(), ids.end());
    return ids;
}

Matches matches_at(const jss::Trie& trie, const std::u32string& text, std::size_t start) {
    Matches out;
    for (const auto& match : trie.common_prefix_search(text, start)) {
        std::vector<int> ids = match.ids;
        std::sort(ids.begin(), ids.end());
        out.emplace_back(match.length, std::move(ids));
    }
    return out;
}

using Ids = std::vector<int>;

// --- Oracle ---------------------------------------------------------------

struct Key {
    std::u32string word;
    int id;
};

// The slow, obviously correct version: compare every key at this position.
Matches brute_force_matches(const std::vector<Key>& keys, const std::u32string& text,
                            std::size_t start) {
    std::map<std::size_t, std::vector<int>> by_length;
    for (const Key& key : keys) {
        if (start < text.size() && text.compare(start, key.word.size(), key.word) == 0) {
            by_length[key.word.size()].push_back(key.id);
        }
    }

    Matches out;
    for (auto& [length, ids] : by_length) {
        std::sort(ids.begin(), ids.end());
        out.emplace_back(length, std::move(ids));
    }
    return out;
}

// rng() % n rather than uniform_int_distribution, whose output differs between
// standard libraries: a seed that fails on CI has to fail on Windows too.
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

std::vector<Key> random_keys(std::mt19937& rng, const std::u32string& alphabet, std::size_t count) {
    std::vector<Key> keys;
    for (std::size_t i = 0; i < count; ++i) {
        keys.push_back({random_word(rng, alphabet, 8), static_cast<int>(i)});
    }
    return keys;
}

// Mostly real keys glued together with some noise, so long matches really occur.
std::u32string random_text(std::mt19937& rng, const std::u32string& alphabet,
                           const std::vector<Key>& keys) {
    std::u32string text;
    while (text.size() < 200) {
        if (random_below(rng, 4) == 0) {
            text += random_word(rng, alphabet, 3);
        } else {
            text += keys[random_below(rng, keys.size())].word;
        }
    }
    return text;
}

}  // namespace

// --- Basics ---------------------------------------------------------------

TEST_CASE("an empty trie finds nothing", "[trie]") {
    const jss::Trie trie;
    REQUIRE(ids_of(trie, U"猫").empty());
    REQUIRE(ids_of(trie, U"").empty());
    REQUIRE(matches_at(trie, U"猫", 0).empty());
}

TEST_CASE("find returns the id of an inserted word", "[trie]") {
    jss::Trie trie;
    trie.insert(U"猫", 7);
    trie.insert(U"食べる", 8);

    REQUIRE(ids_of(trie, U"猫") == Ids{7});
    REQUIRE(ids_of(trie, U"食べる") == Ids{8});
    REQUIRE(ids_of(trie, U"犬").empty());
}

TEST_CASE("id 0 is a real id", "[trie]") {
    jss::Trie trie;
    trie.insert(U"猫", 0);

    REQUIRE(ids_of(trie, U"猫") == Ids{0});
    REQUIRE(matches_at(trie, U"猫", 0) == Matches{{1, {0}}});
}

TEST_CASE("a prefix or extension of a word is not that word", "[trie]") {
    jss::Trie trie;
    trie.insert(U"食べる", 1);

    REQUIRE(ids_of(trie, U"食").empty());
    REQUIRE(ids_of(trie, U"食べ").empty());
    REQUIRE(ids_of(trie, U"食べるな").empty());
    REQUIRE(ids_of(trie, U"").empty());
}

TEST_CASE("words that share a prefix stay separate", "[trie]") {
    jss::Trie trie;
    trie.insert(U"日本", 1);
    trie.insert(U"日本語", 2);
    trie.insert(U"日曜", 3);
    trie.insert(U"日", 4);

    REQUIRE(ids_of(trie, U"日") == Ids{4});
    REQUIRE(ids_of(trie, U"日本") == Ids{1});
    REQUIRE(ids_of(trie, U"日本語") == Ids{2});
    REQUIRE(ids_of(trie, U"日曜") == Ids{3});
    REQUIRE(ids_of(trie, U"日曜日").empty());
}

TEST_CASE("homographs keep every id", "[trie]") {
    jss::Trie trie;
    trie.insert(U"はし", 30);  // bridge
    trie.insert(U"はし", 10);  // chopsticks
    trie.insert(U"はし", 20);  // edge

    REQUIRE(ids_of(trie, U"はし") == Ids{10, 20, 30});
    REQUIRE(matches_at(trie, U"はし", 0) == Matches{{2, {10, 20, 30}}});
}

TEST_CASE("inserting a word that is a prefix of an existing word works", "[trie]") {
    jss::Trie trie;
    trie.insert(U"日本語", 1);
    trie.insert(U"日本", 2);

    REQUIRE(ids_of(trie, U"日本") == Ids{2});
    REQUIRE(ids_of(trie, U"日本語") == Ids{1});
}

// --- Children: kept sorted, found by binary search ------------------------

TEST_CASE("children can arrive in any order", "[trie]") {
    const std::u32string letters = U"あいうえおかきくけこ";

    auto check_all = [&](const jss::Trie& trie) {
        for (std::size_t i = 0; i < letters.size(); ++i) {
            REQUIRE(ids_of(trie, letters.substr(i, 1)) == Ids{static_cast<int>(i)});
        }
        REQUIRE(ids_of(trie, U"ん").empty());  // sorts after every child
        REQUIRE(ids_of(trie, U"ぁ").empty());  // sorts before every child
        REQUIRE(ids_of(trie, U"ぃ").empty());  // sorts between children
    };

    SECTION("ascending") {
        jss::Trie trie;
        for (std::size_t i = 0; i < letters.size(); ++i) {
            trie.insert(letters.substr(i, 1), static_cast<int>(i));
        }
        check_all(trie);
    }
    SECTION("descending") {
        jss::Trie trie;
        for (std::size_t i = letters.size(); i-- > 0;) {
            trie.insert(letters.substr(i, 1), static_cast<int>(i));
        }
        check_all(trie);
    }
    SECTION("alternating ends") {
        jss::Trie trie;
        std::size_t low = 0;
        std::size_t high = letters.size();
        while (low < high) {
            trie.insert(letters.substr(low, 1), static_cast<int>(low));
            ++low;
            if (low < high) {
                --high;
                trie.insert(letters.substr(high, 1), static_cast<int>(high));
            }
        }
        check_all(trie);
    }
}

TEST_CASE("a node can have thousands of children", "[trie]") {
    const int count = 5000;
    const char32_t first = 0x4E00;  // the start of the main kanji block

    jss::Trie trie;
    for (int i = count - 1; i >= 0; --i) {
        trie.insert(std::u32string(1, first + static_cast<char32_t>(i)), i);
    }
    for (int i = 0; i < count; ++i) {
        const std::u32string word(1, first + static_cast<char32_t>(i));
        if (ids_of(trie, word) != Ids{i}) {
            FAIL("wrong result for child " << i);
        }
    }
    REQUIRE(ids_of(trie, std::u32string(1, first + count)).empty());
    REQUIRE(ids_of(trie, std::u32string(1, first - 1)).empty());
}

TEST_CASE("keys can use the whole code point range", "[trie]") {
    jss::Trie trie;
    trie.insert(U"a", 1);
    trie.insert(U"猫", 2);
    trie.insert(U"\U00020BB7", 3);  // 𠮷, outside the 16-bit range
    trie.insert(U"\U0001F600", 4);
    trie.insert(U"\U0010FFFF", 5);
    trie.insert(std::u32string{U'a', 0, U'b'}, 6);  // NUL doesn't end a key

    REQUIRE(ids_of(trie, U"a") == Ids{1});
    REQUIRE(ids_of(trie, U"猫") == Ids{2});
    REQUIRE(ids_of(trie, U"\U00020BB7") == Ids{3});
    REQUIRE(ids_of(trie, U"\U0001F600") == Ids{4});
    REQUIRE(ids_of(trie, U"\U0010FFFF") == Ids{5});
    REQUIRE(ids_of(trie, std::u32string{U'a', 0, U'b'}) == Ids{6});
    // Same low 16 bits as 𠮷, so this fails if code points get truncated.
    REQUIRE(ids_of(trie, U"\U00010BB7").empty());
}

TEST_CASE("a very long key works", "[trie]") {
    const std::u32string word(10000, U'あ');
    jss::Trie trie;
    trie.insert(word, 1);

    REQUIRE(ids_of(trie, word) == Ids{1});
    REQUIRE(ids_of(trie, word.substr(0, 9999)).empty());
    REQUIRE(matches_at(trie, word, 0) == Matches{{10000, {1}}});
}

// --- Common-prefix search -------------------------------------------------

TEST_CASE("common-prefix search returns every word at a position", "[trie]") {
    jss::Trie trie;
    trie.insert(U"日", 1);
    trie.insert(U"日本", 2);
    trie.insert(U"日本語", 3);
    trie.insert(U"本", 4);
    trie.insert(U"語", 5);

    const std::u32string text = U"日本語です";

    REQUIRE(matches_at(trie, text, 0) == Matches{{1, {1}}, {2, {2}}, {3, {3}}});
    REQUIRE(matches_at(trie, text, 1) == Matches{{1, {4}}});
    REQUIRE(matches_at(trie, text, 2) == Matches{{1, {5}}});
    REQUIRE(matches_at(trie, text, 3).empty());
}

TEST_CASE("common-prefix search skips prefixes that are not words", "[trie]") {
    jss::Trie trie;
    trie.insert(U"食べる", 1);
    trie.insert(U"食べ物屋", 2);

    REQUIRE(matches_at(trie, U"食べ物です", 0).empty());
    REQUIRE(matches_at(trie, U"食べ物屋です", 0) == Matches{{4, {2}}});
}

TEST_CASE("common-prefix search stops at the end of the text", "[trie]") {
    jss::Trie trie;
    trie.insert(U"日", 1);
    trie.insert(U"日本語", 2);

    REQUIRE(matches_at(trie, U"日本", 0) == Matches{{1, {1}}});
}

TEST_CASE("common-prefix search doesn't read the string's terminator", "[trie]") {
    // Reading text[text.size()] gives NUL, so keys with a NUL expose the overrun.
    jss::Trie trie;
    trie.insert(std::u32string{U'猫', 0}, 1);
    trie.insert(std::u32string(1, 0), 2);

    REQUIRE(matches_at(trie, U"猫", 0).empty());
    REQUIRE(matches_at(trie, U"猫", 1).empty());
    REQUIRE(matches_at(trie, U"", 0).empty());
}

TEST_CASE("common-prefix search handles positions at and past the end", "[trie]") {
    jss::Trie trie;
    trie.insert(U"猫", 1);

    REQUIRE(matches_at(trie, U"猫", 1).empty());
    REQUIRE(matches_at(trie, U"猫", 2).empty());
    REQUIRE(matches_at(trie, U"猫", 100).empty());
    REQUIRE(matches_at(trie, U"", 0).empty());
}

TEST_CASE("common-prefix search never reports an empty match", "[trie]") {
    // A zero-length match would put a node with start == end in the lattice.
    jss::Trie trie;
    trie.insert(U"", 1);
    trie.insert(U"猫", 2);

    REQUIRE(matches_at(trie, U"猫", 0) == Matches{{1, {2}}});
    REQUIRE(matches_at(trie, U"犬", 0).empty());
    REQUIRE(matches_at(trie, U"", 0).empty());
}

TEST_CASE("common-prefix search on すもももももももものうち", "[trie]") {
    jss::Trie trie;
    trie.insert(U"すもも", 1);
    trie.insert(U"もも", 2);
    trie.insert(U"も", 3);
    trie.insert(U"の", 4);
    trie.insert(U"うち", 5);

    const std::u32string text = U"すもももももももものうち";

    REQUIRE(matches_at(trie, text, 0) == Matches{{3, {1}}});
    REQUIRE(matches_at(trie, text, 1) == Matches{{1, {3}}, {2, {2}}});
    REQUIRE(matches_at(trie, text, 8) == Matches{{1, {3}}});  // the last も
    REQUIRE(matches_at(trie, text, 9) == Matches{{1, {4}}});
    REQUIRE(matches_at(trie, text, 10) == Matches{{2, {5}}});
    REQUIRE(matches_at(trie, text, 11).empty());
}

// --- Randomized oracle ----------------------------------------------------

TEST_CASE("the trie agrees with a brute-force scan on random dictionaries", "[trie][oracle]") {
    const std::size_t dictionary_size = GENERATE(1, 2, 10, 100, 1000, 5000);
    const std::u32string alphabet = GENERATE(as<std::u32string>{},
                                             U"ab",                            // heavy sharing
                                             U"あいうえお",                     // kana
                                             U"a猫\U00020BB7\U0001F600\U0010FFFF");  // all widths
    const std::uint32_t seed = GENERATE(1u, 2u, 3u);

    // Printed if a check fails, so the failing case can be reproduced.
    CAPTURE(dictionary_size, seed, jss::encode_utf8(alphabet));

    std::mt19937 rng(seed);
    const std::vector<Key> keys = random_keys(rng, alphabet, dictionary_size);

    std::map<std::u32string, std::vector<int>> expected_ids;
    jss::Trie trie;
    for (const Key& key : keys) {
        trie.insert(key.word, key.id);
        expected_ids[key.word].push_back(key.id);
    }

    SECTION("find, on every key and its prefixes") {
        for (const Key& key : keys) {
            for (std::size_t length = 1; length <= key.word.size(); ++length) {
                const std::u32string word = key.word.substr(0, length);
                const auto it = expected_ids.find(word);
                const std::vector<int> expected = it == expected_ids.end() ? Ids{} : it->second;
                CAPTURE(jss::encode_utf8(word));
                REQUIRE(ids_of(trie, word) == expected);
            }
        }
    }

    SECTION("find, on random words") {
        for (int i = 0; i < 2000; ++i) {
            const std::u32string word = random_word(rng, alphabet, 10);
            const auto it = expected_ids.find(word);
            const std::vector<int> expected = it == expected_ids.end() ? Ids{} : it->second;
            CAPTURE(jss::encode_utf8(word));
            REQUIRE(ids_of(trie, word) == expected);
        }
    }

    SECTION("common-prefix search, at every position of random texts") {
        std::size_t total_matches = 0;
        for (int i = 0; i < 5; ++i) {
            const std::u32string text = random_text(rng, alphabet, keys);
            // One past the end is included: it is a valid position with no matches.
            for (std::size_t start = 0; start <= text.size(); ++start) {
                const Matches expected = brute_force_matches(keys, text, start);
                total_matches += expected.size();
                CAPTURE(start, jss::encode_utf8(text));
                REQUIRE(matches_at(trie, text, start) == expected);
            }
        }
        // Guards against a generator bug turning this test into nothing.
        REQUIRE(total_matches > 0);
    }

    SECTION("insertion order doesn't matter") {
        jss::Trie reversed;
        for (auto it = keys.rbegin(); it != keys.rend(); ++it) {
            reversed.insert(it->word, it->id);
        }
        const std::u32string text = random_text(rng, alphabet, keys);
        for (std::size_t start = 0; start < text.size(); ++start) {
            REQUIRE(matches_at(reversed, text, start) == matches_at(trie, text, start));
        }
    }
}
