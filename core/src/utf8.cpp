#include "jss/utf8.hpp"

#include <cstddef>

namespace jss {

namespace {

constexpr char32_t kReplacement = 0xFFFD;

// True for values Unicode allows in UTF-8: in range and not a surrogate.
bool is_scalar_value(char32_t cp) {
    return cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF);
}

}  // namespace

std::u32string decode_utf8(std::string_view bytes) {
    std::u32string out;
    out.reserve(bytes.size());

    std::size_t i = 0;
    while (i < bytes.size()) {
        const unsigned char lead = static_cast<unsigned char>(bytes[i]);

        // The lead byte gives the sequence length, its own payload bits, and
        // the smallest value that really needs this length.
        std::size_t length = 0;
        char32_t cp = 0;
        char32_t min_value = 0;
        if (lead < 0x80) {
            length = 1;
            cp = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            length = 2;
            cp = lead & 0x1F;
            min_value = 0x80;
        } else if ((lead & 0xF0) == 0xE0) {
            length = 3;
            cp = lead & 0x0F;
            min_value = 0x800;
        } else if ((lead & 0xF8) == 0xF0) {
            length = 4;
            cp = lead & 0x07;
            min_value = 0x10000;
        } else {
            // A continuation byte with no lead, or 0xF8-0xFF.
            out.push_back(kReplacement);
            ++i;
            continue;
        }

        bool valid = length <= bytes.size() - i;
        for (std::size_t k = 1; valid && k < length; ++k) {
            const unsigned char next = static_cast<unsigned char>(bytes[i + k]);
            if ((next & 0xC0) == 0x80) {
                cp = (cp << 6) | (next & 0x3F);
            } else {
                valid = false;
            }
        }

        // cp < min_value is an overlong encoding, e.g. C0 80 for U+0000.
        if (valid && cp >= min_value && is_scalar_value(cp)) {
            out.push_back(cp);
            i += length;
        } else {
            out.push_back(kReplacement);
            ++i;
        }
    }
    return out;
}

std::string encode_utf8(std::u32string_view text) {
    std::string out;
    out.reserve(text.size() * 3);  // Japanese text is mostly 3 bytes per character.

    for (char32_t cp : text) {
        if (!is_scalar_value(cp)) {
            cp = kReplacement;
        }

        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

}  // namespace jss
