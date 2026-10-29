#include <iostream>
#include <string>

#include "jss/version.hpp"

// Reads Japanese text from stdin, one sentence per line.
// Segmentation is not implemented yet; for now this echoes each line.
int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--version") {
        std::cout << "jss " << jss::version() << "\n";
        return 0;
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        std::cout << line << "\n";
    }
    return 0;
}
