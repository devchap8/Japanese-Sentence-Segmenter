#include <chrono>
#include <cstddef>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "jss/dictionary.hpp"
#include "jss/ranking.hpp"
#include "jss/utf8.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

#ifndef JSS_DEFAULT_DICT
#define JSS_DEFAULT_DICT "data/dict.tsv"
#endif

namespace {

// Command-line arguments as UTF-8. On Windows, argv is in the ANSI code page,
// which can't hold Japanese, so read the wide command line and convert it.
std::vector<std::string> utf8_args(int argc, char** argv) {
    std::vector<std::string> args;
#ifdef _WIN32
    (void)argc;
    (void)argv;
    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    for (int i = 1; i < wargc; ++i) {
        int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(n > 0 ? n - 1 : 0, '\0');
        if (n > 1) {
            WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, s.data(), n, nullptr, nullptr);
        }
        args.push_back(s);
    }
    LocalFree(wargv);
#else
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
#endif
    return args;
}

std::string join(const std::vector<std::string>& parts, const char* sep) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out += sep;
        out += parts[i];
    }
    return out;
}

void print_entry(const jss::Entry& e) {
    std::vector<std::string> kanji;
    for (const auto& k : e.kanji) kanji.push_back(k.text);

    std::vector<std::string> readings;
    for (const auto& r : e.readings) {
        std::string s = r.text;
        if (!r.pitch.empty()) {
            std::vector<std::string> p;
            for (int d : r.pitch) p.push_back(std::to_string(d));
            s += " [" + join(p, ",") + "]";
        }
        readings.push_back(s);
    }

    std::cout << "  ";
    if (!kanji.empty()) std::cout << join(kanji, "; ") << "  ";
    std::cout << "\xE3\x80\x90" << join(readings, "; ") << "\xE3\x80\x91"  // 【 】
              << "  (JMdict " << e.jmdict_id << ")\n";

    for (std::size_t i = 0; i < e.senses.size(); ++i) {
        const auto& s = e.senses[i];
        std::cout << "    " << (i + 1) << ". ";
        if (!s.pos.empty()) std::cout << "(" << join(s.pos, ", ") << ") ";
        std::cout << join(s.glosses, "; ") << "\n";
    }
}

// Every dictionary word the query starts with, longest first; each word's
// entries most common first.
void lookup(const jss::Dictionary& dict, const std::string& query) {
    std::u32string text = jss::decode_utf8(query);
    std::vector<jss::PrefixMatch> matches = dict.trie.common_prefix_search(text, 0);
    jss::rank_prefix_matches(dict, text, 0, matches);

    std::cout << "== " << query << ": " << matches.size()
              << (matches.size() == 1 ? " match\n" : " matches\n");

    for (const jss::PrefixMatch& m : matches) {
        std::string word = jss::encode_utf8(std::u32string_view(text).substr(0, m.length));
        std::cout << "\n" << word << "  (" << m.ids.size()
                  << (m.ids.size() == 1 ? " entry)\n" : " entries)\n");
        for (int id : m.ids) print_entry(dict.entries[static_cast<std::size_t>(id)]);
    }
    std::cout << "\n";
}

int usage() {
    std::cerr << "usage: jss lookup [--dict <path>] <word>...\n"
                 "  Lists every dictionary word each <word> starts with, longest first,\n"
                 "  most common entries first.\n"
                 "  Default dictionary: " JSS_DEFAULT_DICT "\n";
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::vector<std::string> args = utf8_args(argc, argv);
    if (args.empty() || args[0] != "lookup") return usage();

    std::string dict_path = JSS_DEFAULT_DICT;
    std::vector<std::string> words;
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--dict") {
            if (i + 1 == args.size()) return usage();
            dict_path = args[++i];
        } else {
            words.push_back(args[i]);
        }
    }
    if (words.empty()) return usage();

    try {
        auto t0 = std::chrono::steady_clock::now();
        jss::Dictionary dict = jss::load_dictionary_file(dict_path);
        auto t1 = std::chrono::steady_clock::now();
        std::cerr << "loaded " << dict.entries.size() << " entries, " << dict.key_count
                  << " keys in "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()
                  << " ms\n";

        for (const auto& w : words) lookup(dict, w);
    } catch (const std::exception& ex) {
        std::cerr << "jss: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
