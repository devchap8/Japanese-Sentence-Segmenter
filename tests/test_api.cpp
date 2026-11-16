// Tests for the server's JSON answers (server/api.hpp). These call
// search_json directly; no network or HTTP server is involved.
//
// Run just these tests with: build/jss_tests "[api]"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "api.hpp"
#include "jss/dictionary.hpp"

using Catch::Matchers::ContainsSubstring;
using jss::SearchMode;
using nlohmann::json;

namespace {

const jss::Dictionary& mini() {
    static const jss::Dictionary dict =
        jss::load_dictionary_file(std::string(JSS_TEST_DATA_DIR) + "/mini_dict.tsv");
    return dict;
}

jss::Dictionary load(const std::string& text) {
    std::istringstream in(text);
    return jss::load_dictionary(in);
}

std::vector<std::string> keys(const json& response) {
    std::vector<std::string> out;
    for (const json& m : response["matches"]) out.push_back(m["key"].get<std::string>());
    return out;
}

std::vector<int> ids(const json& match) {
    std::vector<int> out;
    for (const json& e : match["entries"]) out.push_back(e["id"].get<int>());
    return out;
}

}  // namespace

TEST_CASE("prefix search lists the words the query starts with, longest first", "[api]") {
    const json r = jss::search_json(mini(), "日本語学校", SearchMode::prefix);
    CHECK(r["mode"] == "prefix");
    CHECK(r["query"] == "日本語学校");
    CHECK(r["searched"] == "日本語学校");
    CHECK(keys(r) == std::vector<std::string>{"日本語", "日本"});
    CHECK(r["total"] == 2);
    CHECK(r["matches"][0]["length"] == 3);
}

TEST_CASE("prefix search reports the trie walk", "[api]") {
    // 日 is a node but not a word in the fixture; 学 has no node under 日本語.
    const json r = jss::search_json(mini(), "日本語学校", SearchMode::prefix);
    REQUIRE(r["path"].size() == 3);
    CHECK(r["path"][0] == json{{"char", "日"}, {"terminal", false}});
    CHECK(r["path"][1] == json{{"char", "本"}, {"terminal", true}});
    CHECK(r["path"][2] == json{{"char", "語"}, {"terminal", true}});
    CHECK(r["walk_stopped"] == true);

    const json whole = jss::search_json(mini(), "日本", SearchMode::prefix);
    CHECK(whole["path"].size() == 2);
    CHECK(whole["walk_stopped"] == false);

    const json none = jss::search_json(mini(), "猫", SearchMode::prefix);
    CHECK(none["path"].empty());
    CHECK(none["walk_stopped"] == true);
    CHECK(none["matches"].empty());
}

TEST_CASE("homographs come most common first", "[api]") {
    // 橋 (nf05) before 箸 (nf19).
    const json r = jss::search_json(mini(), "はし", SearchMode::prefix);
    REQUIRE(keys(r) == std::vector<std::string>{"はし"});
    CHECK(ids(r["matches"][0]) == std::vector<int>{1237410, 1476410});
}

TEST_CASE("entry json has forms, readings, pitch and senses", "[api]") {
    const json r = jss::search_json(mini(), "食べる", SearchMode::prefix);
    REQUIRE(r["matches"].size() == 1);
    const json& e = r["matches"][0]["entries"][0];
    CHECK(e["id"] == 1358280);
    CHECK(e["common"] == true);
    CHECK(e["kanji"][0]["text"] == "食べる");
    CHECK(e["kanji"][0]["priority"] == json{"ichi1", "news2", "nf25"});
    CHECK(e["readings"][0]["text"] == "たべる");
    CHECK(e["readings"][0]["applies_to_all"] == true);
    CHECK(e["readings"][0]["pitch"] == json{2});
    REQUIRE(e["senses"].size() == 2);
    CHECK(e["senses"][0]["pos"] == json{"v1", "vt"});
    CHECK(e["senses"][1]["glosses"].size() == 3);

    const json uncommon = jss::search_json(mini(), "きょう", SearchMode::prefix);
    CHECK(uncommon["matches"][0]["entries"][0]["common"] == false);
}

TEST_CASE("predictive search ranks, then applies the limit", "[api]") {
    // Both keys start with シーディープ; the spec1 one ranks first.
    const json r = jss::search_json(mini(), "シーディープ", SearchMode::predictive, 50);
    CHECK(r["mode"] == "predictive");
    CHECK(keys(r) == std::vector<std::string>{"シーディープレーヤー", "シーディープレイヤー"});

    const json one = jss::search_json(mini(), "シーディープ", SearchMode::predictive, 1);
    CHECK(keys(one) == std::vector<std::string>{"シーディープレーヤー"});
    CHECK(one["total"] == 2);
}

TEST_CASE("predictive search with an empty query returns nothing", "[api]") {
    const json r = jss::search_json(mini(), "", SearchMode::predictive);
    CHECK(r["matches"].empty());
    CHECK(r["total"] == 0);
    const json spaces = jss::search_json(mini(), "  \xE3\x80\x80 ", SearchMode::predictive);  // ideographic space
    CHECK(spaces["matches"].empty());
}

TEST_CASE("katakana queries fall back to hiragana when nothing matches", "[api]") {
    const json r = jss::search_json(mini(), "タベル", SearchMode::prefix);
    CHECK(r["query"] == "タベル");
    CHECK(r["searched"] == "たべる");
    CHECK(keys(r) == std::vector<std::string>{"たべる"});

    // アカン is itself a key, so it is searched as typed.
    const json as_typed = jss::search_json(mini(), "アカン", SearchMode::prefix);
    CHECK(as_typed["searched"] == "アカン");

    const json predictive = jss::search_json(mini(), "タベ", SearchMode::predictive);
    CHECK(predictive["searched"] == "たべ");
    CHECK(keys(predictive) == std::vector<std::string>{"たべる"});
}

TEST_CASE("katakana_to_hiragana leaves other characters alone", "[api]") {
    CHECK(jss::katakana_to_hiragana(U"シーディー") == U"しーでぃー");
    CHECK(jss::katakana_to_hiragana(U"ＣＤ日本ヴ") == U"ＣＤ日本ゔ");
    CHECK(jss::katakana_to_hiragana(U"ヷ") == U"ヷ");  // U+30F7 has no hiragana form
}

TEST_CASE("long queries are cut and flagged", "[api]") {
    std::string query;
    for (int i = 0; i < 100; ++i) query += "日";
    const json r = jss::search_json(mini(), query, SearchMode::prefix);
    CHECK(r["truncated"] == true);
    CHECK(r["query"].get<std::string>().size() == jss::kMaxQueryLength * 3);  // 3 bytes per 日
    CHECK(jss::search_json(mini(), "日本", SearchMode::prefix)["truncated"] == false);
}

TEST_CASE("quotes and backslashes in glosses are escaped", "[api]") {
    const std::string gloss1 = "say \"hi\"";
    const std::string gloss2 = "back\\slash";
    const jss::Dictionary dict = load("E\t1\nR\tてすと\t\t\t\nS\tn\t" + gloss1 + "\t" + gloss2 + "\n");

    const std::string body = jss::search_json(dict, "てすと", SearchMode::prefix).dump();
    CHECK_THAT(body, ContainsSubstring(R"("say \"hi\"")"));
    CHECK_THAT(body, ContainsSubstring(R"("back\\slash")"));

    const json parsed = json::parse(body);
    const json& glosses = parsed["matches"][0]["entries"][0]["senses"][0]["glosses"];
    CHECK(glosses[0] == gloss1);
    CHECK(glosses[1] == gloss2);
}

TEST_CASE("invalid UTF-8 in the query still gives valid JSON", "[api]") {
    const json r = jss::search_json(mini(), "\xFF\xFE日本", SearchMode::prefix);
    CHECK_NOTHROW(r.dump());
    CHECK(r["matches"].empty());  // U+FFFD starts no word
}
