// Tests for ranking by JMdict priority tags (jss/ranking.hpp).
//
// Inline dictionaries put the less common entry first in the file wherever
// possible, so a test can't pass just because file order happens to be right.
//
// Run just these tests with: build/jss_tests "[ranking]"
#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>
#include <vector>

#include "jss/dictionary.hpp"
#include "jss/ranking.hpp"
#include "jss/utf8.hpp"

namespace {

using Tags = std::vector<std::string>;
using Ids = std::vector<int>;

jss::Dictionary load(const std::string& text) {
    std::istringstream in(text);
    return jss::load_dictionary(in);
}

jss::Dictionary load_fixture() {
    return jss::load_dictionary_file(std::string(JSS_TEST_DATA_DIR) + "/mini_dict.tsv");
}

// Keys of the matches, in order, as UTF-8.
std::vector<std::string> keys_of(const std::vector<jss::PredictiveMatch>& matches) {
    std::vector<std::string> out;
    for (const auto& m : matches) out.push_back(jss::encode_utf8(m.key));
    return out;
}

}  // namespace

// --- priority_score -------------------------------------------------------

TEST_CASE("common words score by their nf band", "[ranking]") {
    CHECK(jss::priority_score(Tags{"ichi1", "news1", "nf05"}) == 5);
    CHECK(jss::priority_score(Tags{"news1", "nf01"}) == 1);
    CHECK(jss::priority_score(Tags{"spec2", "nf48"}) == 48);
    CHECK(jss::priority_score(Tags{"news2", "nf25", "spec1"}) == 25);  // spec1 makes it common
}

TEST_CASE("common words without an nf band come after every band", "[ranking]") {
    CHECK(jss::priority_score(Tags{"ichi1"}) == 49);
    CHECK(jss::priority_score(Tags{"spec1"}) == 49);
    CHECK(jss::priority_score(Tags{"gai1"}) == 49);
}

TEST_CASE("tagged but uncommon words come after every common word", "[ranking]") {
    CHECK(jss::priority_score(Tags{"news2", "nf30"}) == 130);
    CHECK(jss::priority_score(Tags{"ichi2"}) == 149);
    CHECK(jss::priority_score(Tags{"gai2"}) == 149);
    CHECK(jss::priority_score(Tags{"news2", "nf30"}) > jss::priority_score(Tags{"ichi1"}));
}

TEST_CASE("untagged words come last", "[ranking]") {
    CHECK(jss::priority_score(Tags{}) == jss::kUntaggedScore);
    CHECK(jss::priority_score(Tags{}) > jss::priority_score(Tags{"ichi2"}));
}

TEST_CASE("priority_score ignores tags it doesn't know", "[ranking]") {
    CHECK(jss::priority_score(Tags{"nf5"}) == 149);     // not two digits
    CHECK(jss::priority_score(Tags{"nfab"}) == 149);
    CHECK(jss::priority_score(Tags{"nf123"}) == 149);
    CHECK(jss::priority_score(Tags{"news1", "zzz"}) == 49);
}

TEST_CASE("the smallest nf band wins", "[ranking]") {
    CHECK(jss::priority_score(Tags{"nf30", "news1", "nf10"}) == 10);
}

// --- form_score -----------------------------------------------------------

TEST_CASE("form_score uses the tags of the spelling that matched", "[ranking]") {
    const jss::Dictionary dict = load_fixture();
    const jss::Entry& taberu = dict.entries.at(0);  // 食べる (nf25), 喰べる (untagged)

    CHECK(jss::form_score(taberu, "食べる") == 25);
    CHECK(jss::form_score(taberu, "たべる") == 25);
    CHECK(jss::form_score(taberu, "喰べる") == jss::kUntaggedScore);
    CHECK(jss::form_score(taberu, "のむ") == jss::kUntaggedScore);
}

TEST_CASE("form_score takes the best form when a spelling appears twice", "[ranking]") {
    const jss::Dictionary dict = load(
        "E\t1\n"
        "K\tカード\t\n"
        "R\tカード\tgai1,nf10\t\t\n"
        "S\tn\tcard\n");
    CHECK(jss::form_score(dict.entries.at(0), "カード") == 10);
}

// --- rank_ids -------------------------------------------------------------

TEST_CASE("rank_ids puts the more common entry first", "[ranking]") {
    const jss::Dictionary dict = load(
        "E\t1\n"
        "K\t端\t\n"
        "R\tはし\tnews2,nf40\t*\t\n"
        "S\tn\tedge\n"
        "E\t2\n"
        "K\t箸\tichi1,news1,nf19\n"
        "R\tはし\tichi1,news1,nf19\t*\t1\n"
        "S\tn\tchopsticks\n"
        "E\t3\n"
        "K\t橋\tichi1,news1,nf05\n"
        "R\tはし\tichi1,news1,nf05\t*\t2\n"
        "S\tn\tbridge\n");

    Ids ids = dict.trie.find(U"はし");
    jss::rank_ids(dict, "はし", ids);
    CHECK(ids == Ids{2, 1, 0});  // bridge (nf05), chopsticks (nf19), edge (uncommon)
}

TEST_CASE("rank_ids ranks by the matched spelling, not the entry's best", "[ranking]") {
    // Both entries can be written 橋; only the first is common under that spelling.
    const jss::Dictionary dict = load_fixture();
    Ids ids = dict.trie.find(U"橋");
    jss::rank_ids(dict, "橋", ids);
    CHECK(ids == Ids{3, 5});  // はし "bridge" (nf05), then きょう "pons" (untagged)

    Ids reversed = {5, 3};
    jss::rank_ids(dict, "橋", reversed);
    CHECK(reversed == Ids{3, 5});
}

TEST_CASE("rank_ids keeps ties in their current order", "[ranking]") {
    const jss::Dictionary dict = load(
        "E\t1\nR\tかき\t\t\t\nS\tn\toyster\n"
        "E\t2\nR\tかき\t\t\t\nS\tn\tpersimmon\n"
        "E\t3\nR\tかき\t\t\t\nS\tn\tfence\n");
    Ids ids = {2, 0, 1};
    jss::rank_ids(dict, "かき", ids);
    CHECK(ids == Ids{2, 0, 1});
}

TEST_CASE("rank_ids on no ids does nothing", "[ranking]") {
    const jss::Dictionary dict = load_fixture();
    Ids ids;
    jss::rank_ids(dict, "はし", ids);
    CHECK(ids.empty());
}

// --- rank_prefix_matches --------------------------------------------------

TEST_CASE("prefix matches come longest first", "[ranking]") {
    const jss::Dictionary dict = load_fixture();
    const std::u32string text = U"日本語学校";
    std::vector<jss::PrefixMatch> matches = dict.trie.common_prefix_search(text, 0);
    jss::rank_prefix_matches(dict, text, 0, matches);

    REQUIRE(matches.size() == 2);
    CHECK(matches[0].length == 3);  // 日本語
    CHECK(matches[0].ids == Ids{2});
    CHECK(matches[1].length == 2);  // 日本
    CHECK(matches[1].ids == Ids{1});
}

TEST_CASE("prefix ranking reads the key from the start position", "[ranking]") {
    const jss::Dictionary dict = load_fixture();
    const std::u32string text = U"この橋";
    std::vector<jss::PrefixMatch> matches = dict.trie.common_prefix_search(text, 2);
    jss::rank_prefix_matches(dict, text, 2, matches);

    REQUIRE(matches.size() == 1);
    CHECK(matches[0].length == 1);
    CHECK(matches[0].ids == Ids{3, 5});
}

// --- rank_predictive_matches ----------------------------------------------

namespace {

// たべ… words. Least common first in the file.
const char* const kTabe =
    "E\t1\nR\tたべすぎ\t\t\t\nS\tn\tovereating\n"
    "E\t2\nR\tたべかた\t\t\t\nS\tn\tway of eating\n"
    "E\t3\nR\tたべものや\t\t\t\nS\tn\tfood shop\n"
    "E\t4\nR\tたべる\tichi1,news2,nf25\t\t\nS\tv1\tto eat\n"
    "E\t5\nR\tたべもの\tichi1,news1,nf09\t\t\nS\tn\tfood\n";

}  // namespace

TEST_CASE("predictive matches sort by score, then length, then code point", "[ranking]") {
    const jss::Dictionary dict = load(kTabe);
    std::vector<jss::PredictiveMatch> matches = dict.trie.predictive_search(U"たべ", 100);
    jss::rank_predictive_matches(dict, matches);

    // たべもの (9), たべる (25), then the untagged ones: 4 characters before
    // 5, and か before す.
    CHECK(keys_of(matches) ==
          std::vector<std::string>{"たべもの", "たべる", "たべかた", "たべすぎ", "たべものや"});
}

TEST_CASE("a predictive key ranks by its most common entry", "[ranking]") {
    const jss::Dictionary dict = load(
        "E\t1\nR\tかき\t\t\t\nS\tn\tfence\n"
        "E\t2\nR\tかく\tnews2,nf40\t\t\nS\tv5k\tto scratch\n"
        "E\t3\nR\tかき\tichi1,news1,nf20\t\t\nS\tn\toyster\n");
    std::vector<jss::PredictiveMatch> matches = dict.trie.predictive_search(U"か", 100);
    jss::rank_predictive_matches(dict, matches);

    REQUIRE(matches.size() == 2);
    CHECK(jss::encode_utf8(matches[0].key) == "かき");  // nf20 beats かく's uncommon nf40
    CHECK(matches[0].ids == Ids{2, 0});              // and its entries are ranked too
    CHECK(jss::encode_utf8(matches[1].key) == "かく");
}

TEST_CASE("ranking no predictive matches does nothing", "[ranking]") {
    const jss::Dictionary dict = load(kTabe);
    std::vector<jss::PredictiveMatch> matches;
    jss::rank_predictive_matches(dict, matches);
    CHECK(matches.empty());
}
