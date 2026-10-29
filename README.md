# Japanese Sentence Segmenter

Splits Japanese text into words. Japanese is written without spaces, so finding word boundaries is the first step for a learner looking words up.

Final project for CSC 212 (Data Structures and Abstractions), University of Rhode Island, Fall 2026.

## How it works (planned)

1. Load JMdict into a **trie** (later a double-array trie).
2. For each position in the input, use the trie's common-prefix search to find every dictionary word starting there, plus conjugated forms via a deinflector. This builds a **word lattice**.
3. Score each word by frequency and each pair of neighbors by part of speech, then run **Viterbi** to pick the cheapest path through the lattice.
4. Show the result in the browser with pitch-accent underlines and a click-to-look-up dictionary popup.

The trie is compared against a hash-set brute force and a sorted array with binary search, and Viterbi is compared against greedy longest match. See `REPORT.md` (coming) for results.

## Build and run

Requires CMake 3.20+ and a C++17 compiler.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure   # run tests
echo "猫が好きです" | ./build/jss             # segment text (not implemented yet)
```

## Layout

| Folder | Contents |
| --- | --- |
| `core/` | C++ library: trie, lattice, cost model, Viterbi |
| `cli/` | Command-line tool that reads text from stdin |
| `tests/` | Unit tests (Catch2) |
| `bench/` | Benchmark harness and plot scripts |
| `data/` | Dictionary preprocessing; sources and licenses |
| `web/` | Browser frontend |
| `docs/` | AI usage log and notes |

## Data credits

Uses [JMdict](https://www.edrdg.org/jmdict/j_jmdict.html) (CC BY-SA 4.0, Electronic Dictionary Research and Development Group) and pitch accent data from [Kanjium](https://github.com/mifunetoshiro/kanjium). See `data/README.md`.
