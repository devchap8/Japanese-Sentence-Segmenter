#pragma once

#include <string>
#include <string_view>

namespace jss {

// Decodes UTF-8 bytes to code points. Never throws: each invalid byte becomes
// U+FFFD and decoding resumes at the next byte, so malformed input still gives
// one code point per position.
std::u32string decode_utf8(std::string_view bytes);

// Encodes code points as UTF-8. Surrogates and values above U+10FFFF become
// U+FFFD, so the output is always valid UTF-8.
std::string encode_utf8(std::u32string_view text);

}  // namespace jss
