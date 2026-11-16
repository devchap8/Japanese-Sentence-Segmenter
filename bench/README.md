# Benchmarks

Every plot in `REPORT.md` comes from the scripts in this folder.

| Structure | Common-prefix query at position i | Predictive query |
| --- | --- | --- |
| `Trie` | one walk down, stops at the first missing child | walk to the prefix node, DFS its subtree |
| `SortedArrayIndex` | narrow a range by binary search, one character at a time | `lower_bound`, then walk forward |
| `HashIndex` | hash every `text[i, i+len)` for len = 1..37 (the longest key) | scan every key, sort the matches |

## Running

```sh
python bench/fetch_corpus.py                      # once: data/corpus.txt, about 1.13M characters
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
./build-release/jss_bench                         # a few minutes; writes bench/results/*.csv
python bench/plot.py                              # needs matplotlib; writes docs/figures/*.png
```

Use a Release build; `jss_bench` warns if it was built without optimization.
`jss_bench --quick` is a few-second smoke run (fewer sizes, one run each).
Other flags: `--dict`, `--corpus`, `--out`, `--runs` (default 5), `--min-ms` (default 50).

## Corpus

`fetch_corpus.py` downloads seven Natsume Sōseki novels from Aozora Bunko (public domain): 吾輩は猫である, こころ, 三四郎, 草枕, 坊っちゃん, それから, 門. It strips the header, colophon, ruby and editor's notes, and every space and line break, then prints the corpus's SHA-256 so a run can be matched to its text. The zips are cached in `data/raw/aozora/`.

## What is measured

| File | Plot | What |
| --- | --- | --- |
| `text_length.csv` | `text_length.png` | Common-prefix search at every position of the first n characters, n = 1K to 1M, full dictionary |
| `dict_size.csv` | `dict_size.png` | Same query on a 100K-character text, with a random 10%..100% of the keys (same subset for every structure) |
| `dict_size.csv` | `memory_build.png` | Build time and heap memory at 100% |
| `predictive.csv` | `predictive.png` | Predictive search (no limit), prefixes grouped by result count 1–9, 10–99, …, up to 25 prefixes per group |

- **Timing:** each measurement repeats the work until a run takes at least 50 ms, divides by the repeat count, and keeps the median of 5 runs.
- **Memory:** `bench.cpp` replaces the global `operator new`/`delete` and counts the bytes each structure asks for while it is built. Allocator overhead (often 16 bytes or more per block) is not included, so the number of blocks is recorded as well; the trie and the hash map have more than a million each.
- **Correctness:** every structure's results are checksummed, and the run stops with an error if any two structures disagree on any query.
