// Tests for the baselines (jss/baselines.hpp) and the equivalence test: the
// trie, the hash index and the sorted array must answer every query the same.
//
// The basic cases are TEMPLATE_TEST_CASEs, so they run once per structure.
//
// Run just these tests with: build/jss_tests "[baselines]"
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "jss/baselines.hpp"
#include "jss/dictionary.hpp"
#include "jss/trie.hpp"
#include "jss/utf8.hpp"

namespace {

using Ids = std::vector<int>;
// (length, ids) and (key, ids). Order and id order both matter here: the
// baselines promise the trie's exact output, not just the same sets.
using Prefix = std::vector<std::pair<std::size_t, Ids>>;
using Predicted = std::vector<std::pair<std::u32string, Ids>>;

constexpr std::size_t kNoLimit = std::numeric_limits<std::size_t>::max();

void build(jss::Trie&) {}
void build(jss::HashIndex& index) { index.build(); }
void build(jss::SortedArrayIndex& index) { index.build(); }

template <class Index>
Prefix prefix_at(const Index& index, const std::u32string& text, std::size_t start) {
    Prefix out;
    for (const auto& m : index.common_prefix_search(text, start)) out.emplace_back(m.length, m.ids);
    return out;
}

template <class Index>
Predicted predict(const Index& index, const std::u32string& prefix, std::size_t limit = kNoLimit) {
    Predicted out;
    for (const auto& m : index.predictive_search(prefix, limit)) out.emplace_back(m.key, m.ids);
    return out;
}

template <class Index>
Index make(const std::vector<std::pair<std::u32string, int>>& keys) {
    Index index;
    for (const auto& [word, id] : keys) index.insert(word, id);
    build(index);
    return index;
}

std::string show(const Predicted& p) {
    std::string out;
    for (const auto& [key, ids] : p) out += jss::encode_utf8(key) + "(" + std::to_string(ids.size()) + ") ";
    return out;
}

}  // namespace

// --- Basics, for every structure ------------------------------------------

#define ALL_INDEXES jss::Trie, jss::HashIndex, jss::SortedArrayIndex

TEMPLATE_TEST_CASE("an empty index finds nothing", "[baselines]", ALL_INDEXES) {
    TestType index;
    build(index);
    CHECK(index.find(U"猫").empty());
    CHECK(prefix_at(index, U"猫", 0).empty());
    CHECK(predict(index, U"").empty());
    CHECK(predict(index, U"猫").empty());
}

TEMPLATE_TEST_CASE("find returns ids in insertion order", "[baselines]", ALL_INDEXES) {
    const auto index = make<TestType>({{U"はし", 30}, {U"はし", 10}, {U"橋", 1}, {U"はし", 20}});
    CHECK(index.find(U"はし") == Ids{30, 10, 20});
    CHECK(index.find(U"橋") == Ids{1});
    CHECK(index.find(U"は").empty());
    CHECK(index.find(U"はしる").empty());
}

TEMPLATE_TEST_CASE("common-prefix search, shortest first", "[baselines]", ALL_INDEXES) {
    const auto index = make<TestType>(
        {{U"日本語", 3}, {U"日", 1}, {U"本", 4}, {U"日本", 2}, {U"日曜", 5}});
    const std::u32string text = U"日本語です";

    CHECK(prefix_at(index, text, 0) == Prefix{{1, {1}}, {2, {2}}, {3, {3}}});
    CHECK(prefix_at(index, text, 1) == Prefix{{1, {4}}});
    CHECK(prefix_at(index, text, 3).empty());
    CHECK(prefix_at(index, text, 5).empty());   // at the end
    CHECK(prefix_at(index, text, 99).empty());  // past the end
}

TEMPLATE_TEST_CASE("common-prefix search skips gaps and stops at the text's end",
                   "[baselines]", ALL_INDEXES) {
    const auto index = make<TestType>({{U"食べる", 1}, {U"食べ物屋", 2}, {U"日本語", 3}, {U"日", 4}});
    CHECK(prefix_at(index, U"食べ物です", 0).empty());
    CHECK(prefix_at(index, U"食べ物屋です", 0) == Prefix{{4, {2}}});
    CHECK(prefix_at(index, U"日本", 0) == Prefix{{1, {4}}});
}

TEMPLATE_TEST_CASE("the empty key is never a search result", "[baselines]", ALL_INDEXES) {
    const auto index = make<TestType>({{U"", 9}, {U"a", 1}});
    CHECK(prefix_at(index, U"a", 0) == Prefix{{1, {1}}});
    CHECK(prefix_at(index, U"b", 0).empty());
    CHECK(predict(index, U"") == Predicted{{U"a", {1}}});
}

TEMPLATE_TEST_CASE("predictive search in code point order with a limit", "[baselines]",
                   ALL_INDEXES) {
    const auto index = make<TestType>({{U"たべる", 1}, {U"たべもの", 2}, {U"たべものや", 3},
                                       {U"たま", 4}, {U"x\U00020BB7", 5}, {U"x￿", 6}});

    CHECK(predict(index, U"たべ") ==
          Predicted{{U"たべもの", {2}}, {U"たべものや", {3}}, {U"たべる", {1}}});
    CHECK(predict(index, U"たべ", 2) == Predicted{{U"たべもの", {2}}, {U"たべものや", {3}}});
    CHECK(predict(index, U"たべ", 0).empty());
    CHECK(predict(index, U"たべるな").empty());
    CHECK(predict(index, U"x") == Predicted{{U"x￿", {6}}, {U"x\U00020BB7", {5}}});
}

TEMPLATE_TEST_CASE("NUL is an ordinary character", "[baselines]", ALL_INDEXES) {
    // key[key.size()] reads as U+0000, so a search that compares one character
    // past a key's end only goes wrong when the text holds a real NUL.
    const std::u32string a_nul{U'a', 0};
    const std::u32string a_nul_b{U'a', 0, U'b'};
    const auto index = make<TestType>({{U"a", 1}, {a_nul, 2}, {a_nul_b, 3}});

    CHECK(index.find(a_nul) == Ids{2});
    CHECK(prefix_at(index, a_nul_b, 0) == Prefix{{1, {1}}, {2, {2}}, {3, {3}}});
    CHECK(prefix_at(index, U"a", 0) == Prefix{{1, {1}}});
    CHECK(predict(index, a_nul) == Predicted{{a_nul, {2}}, {a_nul_b, {3}}});
}

// --- Structure-specific ---------------------------------------------------

TEST_CASE("the sorted array refuses inserts after build", "[baselines]") {
    jss::SortedArrayIndex index;
    index.insert(U"猫", 1);
    index.build();
    CHECK_THROWS_AS(index.insert(U"犬", 2), std::logic_error);
    CHECK(index.find(U"猫") == Ids{1});
}

TEST_CASE("dictionary_keys lists the keys the loader put in the trie", "[baselines]") {
    const jss::Dictionary dict =
        jss::load_dictionary_file(std::string(JSS_TEST_DATA_DIR) + "/mini_dict.tsv");
    const auto keys = jss::dictionary_keys(dict);
    REQUIRE(keys.size() == dict.key_count);

    // A trie rebuilt from the keys answers every key exactly like the loader's.
    const auto rebuilt = make<jss::Trie>(keys);
    for (const auto& [word, id] : keys) {
        CAPTURE(jss::encode_utf8(word));
        CHECK(rebuilt.find(word) == dict.trie.find(word));
    }
    CHECK(predict(rebuilt, U"") == predict(dict.trie, U""));
}

// --- Equivalence on random dictionaries ------------------------------------

namespace {

std::size_t random_below(std::mt19937& rng, std::size_t n) {
    return static_cast<std::size_t>(rng() % static_cast<std::uint32_t>(n));
}

std::u32string random_word(std::mt19937& rng, const std::u32string& alphabet,
                           std::size_t max_length) {
    const std::size_t length = 1 + random_below(rng, max_length);
    std::u32string word;
    for (std::size_t i = 0; i < length; ++i) word.push_back(alphabet[random_below(rng, alphabet.size())]);
    return word;
}

}  // namespace

TEST_CASE("trie, hash index and sorted array agree on random dictionaries",
          "[baselines][equivalence]") {
    const std::size_t dictionary_size = GENERATE(1, 10, 100, 2000);
    const std::u32string alphabet = GENERATE(as<std::u32string>{}, U"ab", U"あいうえお",
                                             U"a猫\U00020BB7\U0001F600\U0010FFFF");
    const std::uint32_t seed = GENERATE(1u, 2u, 3u);
    CAPTURE(dictionary_size, seed, jss::encode_utf8(alphabet));

    std::mt19937 rng(seed);
    std::vector<std::pair<std::u32string, int>> keys;
    for (std::size_t i = 0; i < dictionary_size; ++i) {
        keys.emplace_back(random_word(rng, alphabet, 8), static_cast<int>(i));
    }
    const auto trie = make<jss::Trie>(keys);
    const auto hash = make<jss::HashIndex>(keys);
    const auto sorted = make<jss::SortedArrayIndex>(keys);

    SECTION("find") {
        for (int i = 0; i < 1000; ++i) {
            const std::u32string word = random_below(rng, 2) == 0
                                            ? keys[random_below(rng, keys.size())].first
                                            : random_word(rng, alphabet, 8);
            CAPTURE(jss::encode_utf8(word));
            const Ids expected = trie.find(word);
            REQUIRE(hash.find(word) == expected);
            REQUIRE(sorted.find(word) == expected);
        }
    }

    SECTION("common-prefix search at every position of a text") {
        // Real keys glued together with some noise, so long matches occur.
        std::u32string text;
        while (text.size() < 500) {
            text += random_below(rng, 4) == 0 ? random_word(rng, alphabet, 3)
                                              : keys[random_below(rng, keys.size())].first;
        }
        std::size_t total = 0;
        for (std::size_t start = 0; start <= text.size(); ++start) {
            CAPTURE(start);
            const Prefix expected = prefix_at(trie, text, start);
            total += expected.size();
            REQUIRE(prefix_at(hash, text, start) == expected);
            REQUIRE(prefix_at(sorted, text, start) == expected);
        }
        REQUIRE(total > 0);
    }

    SECTION("predictive search") {
        const std::size_t limit = GENERATE(as<std::size_t>{}, 1, 7, kNoLimit);
        for (int i = 0; i < 300; ++i) {
            const std::u32string& word = keys[random_below(rng, keys.size())].first;
            const std::u32string prefix = word.substr(0, random_below(rng, word.size() + 1));
            CAPTURE(jss::encode_utf8(prefix), limit);
            const Predicted expected = predict(trie, prefix, limit);
            CAPTURE(show(expected));
            REQUIRE(predict(hash, prefix, limit) == expected);
            REQUIRE(predict(sorted, prefix, limit) == expected);
        }
    }
}
