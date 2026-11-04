#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "jss/utf8.hpp"

namespace jss {

struct PrefixMatch {
    std::size_t length;     
    std::vector<int> ids;   
};

class Trie {
    private:
        struct Node {
            std::vector<std::pair<char32_t, int>> children;
            std::vector<int> entry_ids;
        };

        std::vector<Node> nodes;

    public:
        Trie();  

        void insert(const std::u32string& word, int id);

        void insert(std::string_view utf8_word, int id) {
            insert(decode_utf8(utf8_word), id);
        }

        std::vector<int> find(const std::u32string& word) const;

        std::vector<PrefixMatch> common_prefix_search(const std::u32string& text,
                                                    std::size_t start) const;
};

}  // namespace jss
