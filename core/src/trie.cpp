#include "jss/utf8.hpp"
#include "jss/trie.hpp"

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

std::vector<PrefixMatch> Trie::common_prefix_search(const std::u32string& text,
    std::size_t start) const {

        std::vector<PrefixMatch> matches;
        int index = 0;

        for(std::size_t i = start; i < text.size(); i++) {
            const auto& children = nodes[index].children;
            std::size_t pos = find_position(children, text[i]);

            if(pos == children.size() || children[pos].first != text[i]) {
                break; // child does not exist
            }

            index = children[pos].second;

            if(nodes[index].entry_ids.size() > 0) {
                matches.push_back({i - start + 1, nodes[index].entry_ids});
            }
            
        }

        return matches;
}

std::vector<PredictiveMatch> Trie::predictive_search(const std::u32string& prefix,
    std::size_t limit) const {
        int index = 0;
        for(char32_t c : prefix) {
            auto& children = nodes[index].children;
            std::size_t pos = find_position(children, c);     
            
            if (pos < children.size() && children[pos].first == c) {
                index = children[pos].second;  // child exists: move down
                continue;
            } else { // child doesn't exist: return empty vector
                std::vector<PredictiveMatch> empty;
                return empty;
            }
        }

        // Pre-order DFS of the subtree under the prefix node, with an explicit
        // stack (a recursive DFS on a 10,000-character key can overflow the stack).
        // Children are sorted, so pushing them in reverse pops the smallest first,
        // and results come out in code point order.
        struct StackItem {
            int node;
            char32_t c;         // the character on the edge into this node
            std::size_t depth;  // key length at this node
        };

        std::vector<PredictiveMatch> results;
        std::u32string key = prefix;
        std::vector<StackItem> stack;
        stack.push_back({index, 0, prefix.size()});
        bool at_prefix_node = true;

        while (!stack.empty() && results.size() < limit) {
            StackItem item = stack.back();
            stack.pop_back();

            // Rebuild the key: drop whatever the previous branch appended,
            // then add this node's character.
            if (!at_prefix_node) {
                key.resize(item.depth - 1);
                key.push_back(item.c);
            }
            at_prefix_node = false;

            const Node& node = nodes[item.node];
            if (!node.entry_ids.empty() && !key.empty()) {
                results.push_back({key, node.entry_ids});
            }

            for (std::size_t i = node.children.size(); i-- > 0;) {
                stack.push_back({node.children[i].second, node.children[i].first, key.size() + 1});
            }
        }

        return results;
    }


} // namespace jss