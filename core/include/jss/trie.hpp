#include <vector>
#include <utility>
#include <string>

class Trie {
    private:
        struct Node {
            std::vector<std::pair<char32_t, int>> children;
            std::vector<int> entry_ids;
        };
        
        std::vector<Node> nodes;

    public:
        void insert(std::string word, int id);
        std::vector<int> find(std::string word);
        
};