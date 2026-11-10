// Tests for the dict.tsv loader (jss/dictionary.hpp).
//
// Most tests parse small inline strings, so each one shows exactly the input
// it checks. One test loads tests/data/mini_dict.tsv, a few real entries
// copied from dict.tsv. A hidden test, "[.real]", loads the full data/dict.tsv;
// it doesn't run in ctest. Run it by hand after rebuilding the dictionary:
//
//   build/jss_tests "[real]"
//
// Run just these tests with: build/jss_tests "[dictionary]"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "jss/dictionary.hpp"
#include "jss/utf8.hpp"

using Catch::Matchers::ContainsSubstring;

namespace {

jss::Dictionary load(const std::string& text) {
    std::istringstream in(text);
    return jss::load_dictionary(in);
}

// Entry indexes stored under `key`, sorted.
std::vector<int> lookup(const jss::Dictionary& dict, const std::string& key) {
    std::vector<int> ids = dict.trie.find(jss::decode_utf8(key));
    std::sort(ids.begin(), ids.end());
    return ids;
}

// JMdict ids of the entries stored under `key`, sorted.
std::vector<int> jmdict_ids(const jss::Dictionary& dict, const std::string& key) {
    std::vector<int> out;
    for (int index : lookup(dict, key)) {
        out.push_back(dict.entries.at(static_cast<std::size_t>(index)).jmdict_id);
    }
    std::sort(out.begin(), out.end());
    return out;
}

void require_error(const std::string& text, const std::string& message) {
    REQUIRE_THROWS_WITH(load(text), ContainsSubstring(message));
}

const std::string kTaberu =
    "E\t1358280\n"
    "K\t食べる\tichi1,news2,nf25\n"
    "K\t喰べる\t\n"
    "R\tたべる\tichi1,news2,nf25\t*\t2\n"
    "S\tv1,vt\tto eat\n"
    "S\tv1,vt\tto live on (e.g. a salary)\tto live off\tto subsist on\n";

}  // namespace

TEST_CASE("loader reads every field of an entry", "[dictionary]") {
    jss::Dictionary dict = load(kTaberu);
    REQUIRE(dict.entries.size() == 1);
    const jss::Entry& e = dict.entries[0];

    CHECK(e.jmdict_id == 1358280);

    REQUIRE(e.kanji.size() == 2);
    CHECK(e.kanji[0].text == "食べる");
    CHECK(e.kanji[0].priority == std::vector<std::string>{"ichi1", "news2", "nf25"});
    CHECK(e.kanji[1].text == "喰べる");
    CHECK(e.kanji[1].priority.empty());

    REQUIRE(e.readings.size() == 1);
    CHECK(e.readings[0].text == "たべる");
    CHECK(e.readings[0].priority == std::vector<std::string>{"ichi1", "news2", "nf25"});
    CHECK(e.readings[0].applies_to_all);
    CHECK(e.readings[0].applies_to.empty());
    CHECK(e.readings[0].pitch == std::vector<int>{2});

    REQUIRE(e.senses.size() == 2);
    CHECK(e.senses[0].pos == std::vector<std::string>{"v1", "vt"});
    CHECK(e.senses[0].glosses == std::vector<std::string>{"to eat"});
    CHECK(e.senses[1].glosses ==
          std::vector<std::string>{"to live on (e.g. a salary)", "to live off", "to subsist on"});
}

TEST_CASE("loader handles the three kinds of kanji-forms field", "[dictionary]") {
    jss::Dictionary dict = load(
        "E\t1000110\n"
        "K\tＣＤプレーヤー\tspec1\n"
        "K\tＣＤプレイヤー\t\n"
        "R\tシーディープレーヤー\tspec1\tＣＤプレーヤー\t\n"  // a list
        "R\tしーでぃー\t\t*\t\n"                            // all forms
        "R\tシーディー\t\t\t\n"                             // no kanji (re_nokanji)
        "S\tn\tCD player\n");
    const auto& r = dict.entries.at(0).readings;
    REQUIRE(r.size() == 3);

    CHECK_FALSE(r[0].applies_to_all);
    CHECK(r[0].applies_to == std::vector<std::string>{"ＣＤプレーヤー"});

    CHECK(r[1].applies_to_all);
    CHECK(r[1].applies_to.empty());

    CHECK_FALSE(r[2].applies_to_all);
    CHECK(r[2].applies_to.empty());
}

TEST_CASE("loader parses pitch lists", "[dictionary]") {
    jss::Dictionary dict = load(
        "E\t1\n"
        "R\tあからさま\t\t*\t0,3\n"
        "R\tあかん\t\t*\t\n"
        "S\tadj-na\tplain\n");
    const auto& r = dict.entries.at(0).readings;
    CHECK(r[0].pitch == std::vector<int>{0, 3});
    CHECK(r[1].pitch.empty());
}

TEST_CASE("glosses keep their commas", "[dictionary]") {
    jss::Dictionary dict = load(
        "E\t1\n"
        "R\tあ\t\t\t\n"
        "S\tint\tah, oh\tyes, I see\n");
    CHECK(dict.entries.at(0).senses.at(0).glosses ==
          std::vector<std::string>{"ah, oh", "yes, I see"});
}

TEST_CASE("kana-only entries have no kanji forms", "[dictionary]") {
    jss::Dictionary dict = load(
        "E\t2\n"
        "R\tアカン\t\t\t\n"
        "S\texp\tno good\n");
    CHECK(dict.entries.at(0).kanji.empty());
    CHECK(lookup(dict, "アカン") == std::vector<int>{0});
}

TEST_CASE("header comments and CRLF line endings are accepted", "[dictionary]") {
    jss::Dictionary dict = load(
        "# jmdict_date=2026-10-31\r\n"
        "E\t1\r\n"
        "R\tねこ\t\t\t1\r\n"
        "S\tn\tcat\r\n");
    REQUIRE(dict.entries.size() == 1);
    CHECK(dict.entries[0].readings.at(0).text == "ねこ");
    CHECK(dict.entries[0].readings.at(0).pitch == std::vector<int>{1});
    CHECK(dict.entries[0].senses.at(0).glosses == std::vector<std::string>{"cat"});
    CHECK(lookup(dict, "ねこ") == std::vector<int>{0});
}

TEST_CASE("an empty file gives an empty dictionary", "[dictionary]") {
    jss::Dictionary dict = load("");
    CHECK(dict.entries.empty());
    CHECK(dict.key_count == 0);
    CHECK(load("# header only\n").entries.empty());
}

TEST_CASE("every kanji form and reading is a key for its entry index", "[dictionary]") {
    jss::Dictionary dict = load(kTaberu);
    CHECK(lookup(dict, "食べる") == std::vector<int>{0});
    CHECK(lookup(dict, "喰べる") == std::vector<int>{0});
    CHECK(lookup(dict, "たべる") == std::vector<int>{0});
    CHECK(lookup(dict, "食べ").empty());
    CHECK(dict.key_count == 3);
}

TEST_CASE("a spelling repeated within one entry is inserted once", "[dictionary]") {
    // ＣＤ is both a kanji form and a reading here, and listed twice.
    jss::Dictionary dict = load(
        "E\t1\n"
        "K\tＣＤ\t\n"
        "K\tＣＤ\t\n"
        "R\tＣＤ\t\t*\t\n"
        "R\tしーでぃー\t\t*\t\n"
        "S\tn\tCD\n");
    CHECK(lookup(dict, "ＣＤ") == std::vector<int>{0});
    CHECK(dict.key_count == 2);
}

TEST_CASE("errors name the line number", "[dictionary]") {
    SECTION("unknown line type") {
        require_error("E\t1\nX\tfoo\n", "line 2: unknown line type 'X'");
    }
    SECTION("blank line") {
        require_error("E\t1\nR\tあ\t\t\t\n\nS\tn\tah\n", "line 3: unknown line type ''");
    }
    SECTION("line before the first entry") {
        require_error("R\tあ\t\t\t\n", "line 1: 'R' line before the first E line");
    }
    SECTION("wrong field count") {
        require_error("E\t1\nR\tあ\t\t\n", "line 2: expected 5 fields, got 4");
        require_error("E\t1\nK\t亜\n", "line 2: expected 3 fields, got 2");
        require_error("E\n", "line 1: expected 2 fields, got 1");
    }
    SECTION("sense without glosses") {
        require_error("E\t1\nR\tあ\t\t\t\nS\tn\n", "line 3: sense line needs");
    }
    SECTION("bad numbers") {
        require_error("E\tabc\n", "line 1: expected a number, got 'abc'");
        require_error("E\t12x\n", "line 1: expected a number, got '12x'");
        require_error("E\t1\nR\tあ\t\t*\t0,x\n", "line 2: expected a number, got 'x'");
    }
    SECTION("empty form") {
        require_error("E\t1\nR\t\t\t\t\n", "line 2: empty form");
    }
    SECTION("entry without readings or senses") {
        require_error("E\t7\nK\t亜\t\nS\tn\tAsia\nE\t8\n",
                      "line 1: entry 7 has no readings");
        require_error("E\t7\nR\tあ\t\t\t\n", "line 1: entry 7 has no senses");
    }
}

TEST_CASE("fixture file: homographs and common-prefix search", "[dictionary]") {
    jss::Dictionary dict =
        jss::load_dictionary_file(std::string(JSS_TEST_DATA_DIR) + "/mini_dict.tsv");
    REQUIRE(dict.entries.size() == 8);

    // はし is both 橋 (bridge) and 箸 (chopsticks).
    CHECK(jmdict_ids(dict, "はし") == std::vector<int>{1237410, 1476410});
    // 橋 is also a separate entry read きょう.
    CHECK(jmdict_ids(dict, "橋") == std::vector<int>{1237410, 2151440});
    // A re_nokanji reading is still a key.
    CHECK(jmdict_ids(dict, "アカン") == std::vector<int>{1000230});

    // Every word the input starts with: 日本, then 日本語.
    auto matches = dict.trie.common_prefix_search(jss::decode_utf8("日本語学校"), 0);
    REQUIRE(matches.size() == 2);
    CHECK(matches[0].length == 2);
    CHECK(matches[1].length == 3);
    CHECK(dict.entries.at(static_cast<std::size_t>(matches[0].ids.at(0))).jmdict_id == 1582710);
    CHECK(dict.entries.at(static_cast<std::size_t>(matches[1].ids.at(0))).jmdict_id == 1464530);
}

TEST_CASE("missing file is an error", "[dictionary]") {
    REQUIRE_THROWS_WITH(jss::load_dictionary_file("no/such/dict.tsv"),
                        ContainsSubstring("cannot open dictionary file"));
}

TEST_CASE("full data/dict.tsv loads", "[.real]") {
    std::string path = std::string(JSS_DATA_DIR) + "/dict.tsv";
    if (!std::ifstream(path)) {
        SKIP("run data/build_dict.py first");
    }
    auto start = std::chrono::steady_clock::now();
    jss::Dictionary dict = jss::load_dictionary_file(path);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - start)
                  .count();

    WARN("entries: " << dict.entries.size() << ", keys: " << dict.key_count
                     << ", load time: " << ms << " ms");
    CHECK(dict.entries.size() > 200000);
    CHECK_FALSE(jmdict_ids(dict, "食べる").empty());
    CHECK(jmdict_ids(dict, "はし").size() >= 2);
}
