#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <initializer_list>
#include <ios>
#include <string>

#include "jss/utf8.hpp"

namespace {

std::string bytes(std::initializer_list<int> values) {
    std::string out;
    for (int value : values) {
        out.push_back(static_cast<char>(value));
    }
    return out;
}

std::u32string replacements(std::size_t count) {
    return std::u32string(count, U'�');
}

}  // namespace

TEST_CASE("decode gives exact code points", "[utf8]") {
    REQUIRE(jss::decode_utf8("") == U"");
    REQUIRE(jss::decode_utf8("abc") == U"abc");
    REQUIRE(jss::decode_utf8(bytes({0xE7, 0x8C, 0xAB, 0xE3, 0x81, 0x8C})) ==
            std::u32string{0x732B, 0x304C});  // 猫が
    REQUIRE(jss::decode_utf8(bytes({0xF0, 0x9F, 0x98, 0x80})) == std::u32string{0x1F600});
    REQUIRE(jss::decode_utf8("猫が好きです") ==
            std::u32string{0x732B, 0x304C, 0x597D, 0x304D, 0x3067, 0x3059});
}

TEST_CASE("embedded NUL is an ordinary character", "[utf8]") {
    REQUIRE(jss::decode_utf8(bytes({'a', 0x00, 'b'})) == std::u32string{U'a', 0, U'b'});
}

TEST_CASE("encode uses the right length at each boundary", "[utf8]") {
    const auto encoded = [](char32_t cp) { return jss::encode_utf8(std::u32string(1, cp)); };

    REQUIRE(encoded(0x7F) == bytes({0x7F}));
    REQUIRE(encoded(0x80) == bytes({0xC2, 0x80}));
    REQUIRE(encoded(0x7FF) == bytes({0xDF, 0xBF}));
    REQUIRE(encoded(0x800) == bytes({0xE0, 0xA0, 0x80}));
    REQUIRE(encoded(0xFFFF) == bytes({0xEF, 0xBF, 0xBF}));
    REQUIRE(encoded(0x10000) == bytes({0xF0, 0x90, 0x80, 0x80}));
    REQUIRE(encoded(0x10FFFF) == bytes({0xF4, 0x8F, 0xBF, 0xBF}));
}

TEST_CASE("every valid code point round-trips", "[utf8]") {
    for (char32_t cp = 0; cp <= 0x10FFFF; ++cp) {
        if (cp >= 0xD800 && cp <= 0xDFFF) {
            continue;
        }
        const std::u32string text(1, cp);
        if (jss::decode_utf8(jss::encode_utf8(text)) != text) {
            FAIL("round trip failed for U+" << std::hex << static_cast<unsigned long>(cp));
        }
    }
    SUCCEED();
}

TEST_CASE("decode replaces each invalid byte and keeps going", "[utf8]") {
    SECTION("stray continuation byte") {
        REQUIRE(jss::decode_utf8(bytes({0x80})) == replacements(1));
        REQUIRE(jss::decode_utf8(bytes({'a', 0xBF, 'b'})) == std::u32string{U'a', 0xFFFD, U'b'});
    }
    SECTION("sequence truncated by end of input") {
        REQUIRE(jss::decode_utf8(bytes({0xC3})) == replacements(1));
        REQUIRE(jss::decode_utf8(bytes({0xE3, 0x81})) == replacements(2));
        REQUIRE(jss::decode_utf8(bytes({0xF0, 0x9F, 0x98})) == replacements(3));
    }
    SECTION("sequence interrupted by another character") {
        REQUIRE(jss::decode_utf8(bytes({0xE3, 'A'})) == std::u32string{0xFFFD, U'A'});
        REQUIRE(jss::decode_utf8(bytes({0xE3, 0x81, 0xE3, 0x81, 0x8C})) ==
                std::u32string{0xFFFD, 0xFFFD, 0x304C});
    }
    SECTION("overlong encodings") {
        REQUIRE(jss::decode_utf8(bytes({0xC0, 0x80})) == replacements(2));
        REQUIRE(jss::decode_utf8(bytes({0xC1, 0xBF})) == replacements(2));
        REQUIRE(jss::decode_utf8(bytes({0xE0, 0x9F, 0xBF})) == replacements(3));
        REQUIRE(jss::decode_utf8(bytes({0xF0, 0x8F, 0xBF, 0xBF})) == replacements(4));
    }
    SECTION("surrogates") {
        REQUIRE(jss::decode_utf8(bytes({0xED, 0xA0, 0x80})) == replacements(3));
        REQUIRE(jss::decode_utf8(bytes({0xED, 0xBF, 0xBF})) == replacements(3));
    }
    SECTION("values above U+10FFFF") {
        REQUIRE(jss::decode_utf8(bytes({0xF4, 0x90, 0x80, 0x80})) == replacements(4));
    }
    SECTION("bytes that can never appear in UTF-8") {
        REQUIRE(jss::decode_utf8(bytes({0xF8})) == replacements(1));
        REQUIRE(jss::decode_utf8(bytes({0xFE, 0xFF})) == replacements(2));
    }
}

TEST_CASE("a real U+FFFD in the input decodes once", "[utf8]") {
    REQUIRE(jss::decode_utf8(bytes({0xEF, 0xBF, 0xBD})) == replacements(1));
}

TEST_CASE("encode replaces values that are not scalar values", "[utf8]") {
    const std::string replacement = bytes({0xEF, 0xBF, 0xBD});
    REQUIRE(jss::encode_utf8(std::u32string{0xD800}) == replacement);
    REQUIRE(jss::encode_utf8(std::u32string{0xDFFF}) == replacement);
    REQUIRE(jss::encode_utf8(std::u32string{0x110000}) == replacement);
    REQUIRE(jss::encode_utf8(std::u32string{U'a', 0xFFFFFFFF, U'b'}) == "a" + replacement + "b");
}
