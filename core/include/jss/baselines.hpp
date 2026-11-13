#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "jss/dictionary.hpp"
#include "jss/trie.hpp"

namespace jss {

// Two baselines for the trie. Each answers the same queries with the same
// results in the same order as Trie, so tests and benchmarks can swap them:
//
//   find(word)                       ids under exactly `word`, in insertion order
//   common_prefix_search(text, i)    every key that text starts with at i, shortest first
//   predictive_search(prefix, k)     first k keys that start with prefix, in code point order
//
// The empty key is never reported by either search (find("") still returns it).

// Hash map from key to ids. Common-prefix search hashes every substring
// text[i, i+len) for len = 1..longest key: O(K) lookups, each costing O(len)
// to hash. Predictive search has no order to use, so it scans every key.
class HashIndex {
    public:
        void insert(const std::u32string& word, int id);
        void build() {}  // nothing to do; here so every structure is used the same way

        std::vector<int> find(const std::u32string& word) const;
        std::vector<PrefixMatch> common_prefix_search(const std::u32string& text,
                                                      std::size_t start) const;
        std::vector<PredictiveMatch> predictive_search(const std::u32string& prefix,
                                                       std::size_t limit) const;

    private:
        std::unordered_map<std::u32string, std::vector<int>> map_;
        std::size_t longest_key_ = 0;
};

// Sorted array of (key, ids), searched by binary search.
// Call build() once, after the last insert and before any query; inserting
// after build() throws std::logic_error. (Sorting once is the point of this
// structure; supporting inserts afterwards would mean O(N) per insert.)
//
// Common-prefix search narrows a range one character at a time: the keys that
// start with text[i, i+d) are one contiguous run of the array, so the run for
// d+1 characters is found by binary search inside the run for d. O(K log N).
// Predictive search is lower_bound(prefix), then a walk forward: O(log N + k).
class SortedArrayIndex {
    public:
        void insert(const std::u32string& word, int id);
        void build();  // sorts and groups duplicate keys; ids keep insertion order

        std::vector<int> find(const std::u32string& word) const;
        std::vector<PrefixMatch> common_prefix_search(const std::u32string& text,
                                                      std::size_t start) const;
        std::vector<PredictiveMatch> predictive_search(const std::u32string& prefix,
                                                       std::size_t limit) const;

    private:
        struct Item {
            std::u32string key;
            std::vector<int> ids;
        };
        std::vector<std::pair<std::u32string, int>> pending_;  // inserted, not yet built
        std::vector<Item> items_;                              // sorted by key, unique
        bool built_ = false;
};

// Every (key, entry index) pair of a loaded dictionary, in the order the loader
// inserted them into dict.trie: per entry, kanji forms then readings, each
// spelling once. Use it to build the baselines from the same keys as the trie.
std::vector<std::pair<std::u32string, int>> dictionary_keys(const Dictionary& dict);

}  // namespace jss
