#include "trie.hpp";
#include "utf8.hpp";

#include <vector>
#include <utility>
#include <string>

namespace jss {

Trie::Trie() {
    Node head = {};
    nodes.push_back(head);
}

// helper function: binary search for position to insert character while preserving order
std::size_t find_position(const std::vector<std::pair<char32_t, int>>& children,
                        char32_t c) {
    std::size_t low = 0;
    std::size_t high = children.size();
    while (low < high) {
        std::size_t mid = low + (high - low) / 2;
        if (children[mid].first < c) {
            low = mid + 1;   
        } else {
            high = mid;      
        }
    }
    return low;
}

void Trie::insert(const std::u32string& word, int id) {
    int index = 0;
    for (char32_t c : word) {
        auto& children = nodes[index].children;
        std::size_t pos = find_position(children, c);

        if (pos < children.size() && children[pos].first == c) {
            index = children[pos].second;  // child exists: move down
            continue;
        }

        int new_index = static_cast<int>(nodes.size());
        nodes.push_back(Node{}); 

        nodes[index].children.insert(nodes[index].children.begin() + pos, {c, new_index});
        index = new_index;
    }
    nodes[index].entry_ids.push_back(id);
}

std::vector<int> Trie::find(const std::u32string& word) const {
    int index = 0;
    for(char32_t c : word) {
        auto& children = nodes[index].children;
        std::size_t pos = find_position(children, c);     
        
        if (pos < children.size() && children[pos].first == c) {
            index = children[pos].second;  // child exists: move down
            continue;
        } else { // child doesn't exist: return empty vector
            std::vector<int> empty;
            return empty;
        }
    }
    return nodes[index].entry_ids;
}



} // namespace jss