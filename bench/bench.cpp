// Benchmark harness: Trie vs. HashIndex vs. SortedArrayIndex.
//
// Writes three CSV files (plotted by bench/plot.py):
//
//   text_length.csv   common-prefix search at every position of the first n
//                     characters of the corpus, full dictionary          (plot 1)
//   dict_size.csv     same query on a fixed text, with 10%..100% of the
//                     dictionary's keys; also build time and heap memory (plots 2, 3)
//   predictive.csv    predictive search, grouped by how many keys the
//                     prefix has under it                                (plot 4)
//
// Timing: each measurement repeats the work until it takes at least
// --min-ms, divides by the repeat count, and keeps the median of --runs runs.
// Memory: this file replaces the global operator new/delete to count the heap
// bytes each structure requests (allocator overhead is not included; the
// allocation count is reported so it can be estimated).
//
// Every structure's results are checksummed; the run aborts if they differ.
//
// Usage: jss_bench [--dict data/dict.tsv] [--corpus data/corpus.txt]
//                  [--out bench/results] [--runs 5] [--min-ms 50] [--quick]
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <new>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "jss/baselines.hpp"
#include "jss/dictionary.hpp"
#include "jss/trie.hpp"
#include "jss/utf8.hpp"

// --- Heap accounting ------------------------------------------------------
//
// Each block gets a header holding its size, so delete can subtract it.
// Only the plain and array forms are replaced; nothing in this program uses
// over-aligned types, so the aligned forms are never called.

namespace {

std::size_t g_heap_bytes = 0;   // bytes currently allocated
std::size_t g_heap_blocks = 0;  // blocks currently allocated
constexpr std::size_t kHeader = alignof(std::max_align_t);

void* counted_alloc(std::size_t size) {
    void* raw = std::malloc(size + kHeader);
    if (raw == nullptr) return nullptr;
    *static_cast<std::size_t*>(raw) = size;
    g_heap_bytes += size;
    ++g_heap_blocks;
    return static_cast<char*>(raw) + kHeader;
}

void counted_free(void* p) noexcept {
    if (p == nullptr) return;
    void* raw = static_cast<char*>(p) - kHeader;
    g_heap_bytes -= *static_cast<std::size_t*>(raw);
    --g_heap_blocks;
    std::free(raw);
}

}  // namespace

void* operator new(std::size_t size) {
    if (void* p = counted_alloc(size)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return operator new(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return counted_alloc(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return counted_alloc(size); }
void operator delete(void* p) noexcept { counted_free(p); }
void operator delete[](void* p) noexcept { counted_free(p); }
void operator delete(void* p, std::size_t) noexcept { counted_free(p); }
void operator delete[](void* p, std::size_t) noexcept { counted_free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { counted_free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { counted_free(p); }

namespace {

using Clock = std::chrono::steady_clock;
using Keys = std::vector<std::pair<std::u32string, int>>;

struct Options {
    std::string dict_path = JSS_DEFAULT_DICT;
    std::string corpus_path = JSS_DEFAULT_CORPUS;
    std::string out_dir = JSS_DEFAULT_OUT;
    int runs = 5;
    double min_ms = 50.0;
    bool quick = false;
};

// Keeps results alive so the optimizer can't drop the work being timed.
volatile std::size_t g_sink = 0;

double ms_since(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Median nanoseconds for one call of `work`. `work` returns a checksum.
template <class Work>
double time_ns(const Options& opt, Work&& work) {
    // Calibrate: one call, then enough repeats to reach min_ms per run.
    const auto t0 = Clock::now();
    g_sink = g_sink + work();
    const double one_ms = std::max(ms_since(t0), 1e-6);
    const auto reps = static_cast<std::size_t>(std::max(1.0, std::ceil(opt.min_ms / one_ms)));

    std::vector<double> per_call;
    for (int r = 0; r < opt.runs; ++r) {
        const auto start = Clock::now();
        for (std::size_t i = 0; i < reps; ++i) g_sink = g_sink + work();
        per_call.push_back(ms_since(start) * 1e6 / static_cast<double>(reps));
    }
    std::nth_element(per_call.begin(), per_call.begin() + per_call.size() / 2, per_call.end());
    return per_call[per_call.size() / 2];
}

// --- The three structures, used the same way -------------------------------

const char* const kNames[] = {"trie", "hash", "sorted_array"};

void finish(jss::Trie&) {}
void finish(jss::HashIndex& index) { index.build(); }
void finish(jss::SortedArrayIndex& index) { index.build(); }

template <class Index>
void build_into(Index& index, const Keys& keys) {
    for (const auto& [key, id] : keys) index.insert(key, id);
    finish(index);
}

// Common-prefix search at every position. Checksum: number of (position, key)
// matches times a large odd number, plus the total of their lengths and ids,
// so a structure that finds the same count but different keys still differs.
struct PrefixStats {
    std::uint64_t matches = 0;
    std::uint64_t checksum = 0;
};

template <class Index>
PrefixStats scan_text(const Index& index, const std::u32string& text) {
    PrefixStats stats;
    for (std::size_t i = 0; i < text.size(); ++i) {
        for (const jss::PrefixMatch& m : index.common_prefix_search(text, i)) {
            ++stats.matches;
            stats.checksum += m.length * 1000003u;
            for (int id : m.ids) stats.checksum += static_cast<std::uint64_t>(id);
        }
    }
    return stats;
}

template <class Index>
std::uint64_t predict_all(const Index& index, const std::vector<std::u32string>& prefixes,
                          std::uint64_t* result_count = nullptr) {
    std::uint64_t checksum = 0;
    std::uint64_t count = 0;
    for (const std::u32string& p : prefixes) {
        for (const jss::PredictiveMatch& m : index.predictive_search(p, std::numeric_limits<std::size_t>::max())) {
            ++count;
            checksum += m.key.size() * 1000003u + m.key.back();
            for (int id : m.ids) checksum += static_cast<std::uint64_t>(id);
        }
    }
    if (result_count != nullptr) *result_count = count;
    return checksum;
}

void check_same(const char* what, const std::vector<std::uint64_t>& sums) {
    for (std::size_t s = 1; s < sums.size(); ++s) {
        if (sums[s] != sums[0]) {
            std::ostringstream msg;
            msg << what << ": " << kNames[s] << " gave checksum " << sums[s] << " but "
                << kNames[0] << " gave " << sums[0];
            throw std::runtime_error(msg.str());
        }
    }
}

// Calls fn(index, name_index) once for each structure, built from `keys`.
template <class Fn>
void for_each_structure(const Keys& keys, Fn&& fn) {
    { jss::Trie t; build_into(t, keys); fn(t, 0); }
    { jss::HashIndex h; build_into(h, keys); fn(h, 1); }
    { jss::SortedArrayIndex s; build_into(s, keys); fn(s, 2); }
}

std::ofstream open_csv(const Options& opt, const char* name, const char* header) {
    const auto path = std::filesystem::path(opt.out_dir) / name;
    std::ofstream out(path);
    if (!out) throw std::runtime_error("can't write " + path.string());
    out << header << '\n';
    std::cout << "\n== " << path.string() << '\n';
    return out;
}

// --- Experiment 1: time vs. text length -------------------------------------

void bench_text_length(const Options& opt, const Keys& keys, const std::u32string& corpus) {
    std::vector<std::size_t> lengths;
    for (std::size_t decade = 1000; decade <= 1000000; decade *= 10) {
        for (std::size_t step : {std::size_t{1}, std::size_t{2}, std::size_t{5}}) {
            if (decade * step <= corpus.size()) lengths.push_back(decade * step);
        }
    }
    if (opt.quick) lengths.resize(std::min<std::size_t>(lengths.size(), 4));

    auto csv = open_csv(opt, "text_length.csv", "structure,text_length,ns,ns_per_char,matches");
    std::vector<std::vector<std::uint64_t>> sums(lengths.size());

    for_each_structure(keys, [&](const auto& index, int s) {
        for (std::size_t li = 0; li < lengths.size(); ++li) {
            const std::u32string text = corpus.substr(0, lengths[li]);
            const PrefixStats stats = scan_text(index, text);
            sums[li].push_back(stats.checksum);
            const double ns = time_ns(opt, [&] { return scan_text(index, text).matches; });
            csv << kNames[s] << ',' << lengths[li] << ',' << ns << ','
                << ns / static_cast<double>(lengths[li]) << ',' << stats.matches << '\n';
            std::printf("  %-12s n=%8zu  %10.3f ms  (%6.1f ns/char, %llu matches)\n", kNames[s],
                        lengths[li], ns / 1e6, ns / static_cast<double>(lengths[li]),
                        static_cast<unsigned long long>(stats.matches));
        }
    });
    for (std::size_t li = 0; li < lengths.size(); ++li) check_same("text_length", sums[li]);
}

// --- Experiments 2 and 3: dictionary size, build time, memory ---------------

void bench_dict_size(const Options& opt, const Keys& all_keys, const std::u32string& corpus) {
    const std::size_t text_length = std::min<std::size_t>(corpus.size(), opt.quick ? 10000 : 100000);
    const std::u32string text = corpus.substr(0, text_length);

    // A random subset of the (key, entry) pairs, the same for every structure:
    // shuffle once, take the first p%.
    Keys shuffled = all_keys;
    std::mt19937 rng(212);
    std::shuffle(shuffled.begin(), shuffled.end(), rng);

    std::vector<int> percents = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
    if (opt.quick) percents = {10, 50, 100};
    const int build_runs = opt.quick ? 1 : 3;

    auto csv = open_csv(opt, "dict_size.csv",
                        "structure,percent,keys,text_length,build_ms,heap_bytes,heap_blocks,ns,matches");

    for (int percent : percents) {
        const std::size_t n = all_keys.size() * static_cast<std::size_t>(percent) / 100;
        const Keys keys(shuffled.begin(), shuffled.begin() + static_cast<std::ptrdiff_t>(n));
        std::vector<std::uint64_t> sums;

        auto run = [&](auto make, int s) {
            // Build several times for the build time; keep the last one.
            std::vector<double> build_ms;
            std::size_t heap_bytes = 0, heap_blocks = 0;
            auto index = make();
            for (int r = 0; r < build_runs; ++r) {
                index = make();  // frees the previous build before measuring
                const std::size_t bytes_before = g_heap_bytes, blocks_before = g_heap_blocks;
                const auto start = Clock::now();
                build_into(index, keys);
                build_ms.push_back(ms_since(start));
                heap_bytes = g_heap_bytes - bytes_before;
                heap_blocks = g_heap_blocks - blocks_before;
            }
            std::sort(build_ms.begin(), build_ms.end());
            const double build = build_ms[build_ms.size() / 2];

            const PrefixStats stats = scan_text(index, text);
            sums.push_back(stats.checksum);
            const double ns = time_ns(opt, [&] { return scan_text(index, text).matches; });

            csv << kNames[s] << ',' << percent << ',' << n << ',' << text_length << ',' << build << ','
                << heap_bytes << ',' << heap_blocks << ',' << ns << ',' << stats.matches << '\n';
            std::printf("  %-12s %3d%% (%7zu keys)  build %8.1f ms  heap %7.1f MB in %8zu blocks  query %8.3f ms\n",
                        kNames[s], percent, n, build, static_cast<double>(heap_bytes) / 1048576.0,
                        heap_blocks, ns / 1e6);
        };
        run([] { return jss::Trie(); }, 0);
        run([] { return jss::HashIndex(); }, 1);
        run([] { return jss::SortedArrayIndex(); }, 2);
        check_same("dict_size", sums);
    }
}

// --- Experiment 4: predictive search vs. number of results ------------------

void bench_predictive(const Options& opt, const Keys& keys) {
    // Distinct keys in code point order, to count each prefix's results.
    std::vector<std::u32string> sorted;
    sorted.reserve(keys.size());
    for (const auto& kv : keys) sorted.push_back(kv.first);
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

    auto results_for = [&](const std::u32string& prefix) {
        const auto lo = std::lower_bound(sorted.begin(), sorted.end(), prefix);
        auto hi = lo;
        while (hi != sorted.end() && hi->compare(0, prefix.size(), prefix) == 0) ++hi;
        return static_cast<std::size_t>(hi - lo);
    };

    // Candidate prefixes: the first 1, 2 and 3 characters of every key.
    std::vector<std::u32string> candidates;
    for (const std::u32string& k : sorted) {
        for (std::size_t len = 1; len <= 3 && len <= k.size(); ++len) candidates.push_back(k.substr(0, len));
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

    // Bucket by result count, 1-9, 10-99, ..., and sample a fixed number per bucket.
    const std::size_t per_bucket = opt.quick ? 5 : 25;
    std::vector<std::vector<std::u32string>> buckets(6);
    std::mt19937 rng(212);
    std::shuffle(candidates.begin(), candidates.end(), rng);
    for (const std::u32string& p : candidates) {
        std::size_t count = results_for(p);
        std::size_t b = 0;
        while (count >= 10 && b + 1 < buckets.size()) { count /= 10; ++b; }
        if (buckets[b].size() < per_bucket) buckets[b].push_back(p);
    }

    auto csv = open_csv(opt, "predictive.csv", "structure,bucket_min,queries,mean_results,ns_per_query");
    std::vector<std::vector<std::uint64_t>> sums(buckets.size());

    for_each_structure(keys, [&](const auto& index, int s) {
        std::size_t bucket_min = 1;
        for (std::size_t b = 0; b < buckets.size(); ++b, bucket_min *= 10) {
            const auto& prefixes = buckets[b];
            if (prefixes.empty()) continue;
            std::uint64_t results = 0;
            sums[b].push_back(predict_all(index, prefixes, &results));
            const double ns = time_ns(opt, [&] { return predict_all(index, prefixes); }) /
                              static_cast<double>(prefixes.size());
            const double mean = static_cast<double>(results) / static_cast<double>(prefixes.size());
            csv << kNames[s] << ',' << bucket_min << ',' << prefixes.size() << ',' << mean << ',' << ns << '\n';
            std::printf("  %-12s %6zu+ results (%2zu prefixes, mean %8.1f)  %10.1f us/query\n", kNames[s],
                        bucket_min, prefixes.size(), mean, ns / 1e3);
        }
    });
    for (std::size_t b = 0; b < buckets.size(); ++b) {
        if (!sums[b].empty()) check_same("predictive", sums[b]);
    }
}

// --- Main -------------------------------------------------------------------

std::u32string read_corpus(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("can't open " + path + " (run: python bench/fetch_corpus.py)");
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return jss::decode_utf8(bytes);
}

Options parse_args(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error(arg + " needs a value");
            return argv[++i];
        };
        if (arg == "--dict") opt.dict_path = value();
        else if (arg == "--corpus") opt.corpus_path = value();
        else if (arg == "--out") opt.out_dir = value();
        else if (arg == "--runs") opt.runs = std::max(1, std::stoi(value()));
        else if (arg == "--min-ms") opt.min_ms = std::stod(value());
        else if (arg == "--quick") { opt.quick = true; opt.runs = 1; opt.min_ms = 1; }
        else throw std::runtime_error("unknown argument: " + arg);
    }
    return opt;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options opt = parse_args(argc, argv);
#ifndef NDEBUG
        std::cerr << "warning: this is a debug build; timings are not meaningful.\n"
                     "         configure with -DCMAKE_BUILD_TYPE=Release.\n";
#endif
        std::filesystem::create_directories(opt.out_dir);

        std::cout << "loading " << opt.dict_path << '\n';
        Keys keys;
        {
            const jss::Dictionary dict = jss::load_dictionary_file(opt.dict_path);
            keys = jss::dictionary_keys(dict);
        }  // free the dictionary's own trie and entries before measuring
        const std::u32string corpus = read_corpus(opt.corpus_path);
        std::size_t longest = 0;
        for (const auto& kv : keys) longest = std::max(longest, kv.first.size());
        std::cout << keys.size() << " keys (longest " << longest << " characters), corpus "
                  << corpus.size() << " characters\n";

        bench_text_length(opt, keys, corpus);
        bench_dict_size(opt, keys, corpus);
        bench_predictive(opt, keys);
        std::cout << "\nall structures agreed on every query.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
