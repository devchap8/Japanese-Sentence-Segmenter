# Japanese Sentence Segmenter: Trie Dictionary

A Japanese dictionary lookup built on a trie. Type Japanese and the page lists every dictionary word your input starts with, found in one walk down the trie: 日本語学校 gives 日本語学校, 日本語学, 日本語, 日本 and 日, longest first. A second mode lists the words that *start with* what you typed (たべ gives 食べ物, 食べる, 食べ過ぎる…), ranked by how common they are.

Final project for CSC 212 (Data Structures and Abstractions), University of Rhode Island, Fall 2026. The project started as a full sentence segmenter (`docs/PLAN.md`) and was scoped down to this dictionary (`docs/PLAN-dictionary.md`); the data structure, baselines, benchmarks and tests are at the depth planned for the segmenter.

## How it works

1. `data/build_dict.py` turns JMdict and Kanjium pitch data into `data/dict.tsv`: about 219,000 entries and 500,000 keys (kanji forms and readings).
2. The C++ loader reads it into a `vector<Entry>` and inserts every key into a **pointer trie** whose nodes store indexes into that vector.
3. **Common-prefix search** walks the trie once along the input and reports every node that ends a word. **Predictive search** walks to the prefix's node and collects its subtree with a DFS.
4. Results are ranked by JMdict priority tags (`ichi1`, `news1`, `nfXX`…).
5. `jss_server` loads the dictionary once and answers `GET /api/search`; the page in `web/` (plain JS) shows the results.

The trie is compared with a hash map (hashing every substring) and a sorted array with binary search, on the same queries. See `bench/README.md` and the figures in `docs/figures/`.

## Build and run

Requires CMake 3.25+, a C++17 compiler, and Python 3 for the data script. CMake downloads Catch2, cpp-httplib and nlohmann/json.

```sh
python data/build_dict.py                    # once: data/dict.tsv (downloads JMdict and Kanjium)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure   # tests
./build/jss lookup 日本語学校                 # command-line lookup
./build/jss_server                           # then open http://localhost:8080
```

`jss_server` takes `--dict`, `--web`, `--host` and `--port` (default `127.0.0.1:8080`; `--host 0.0.0.0` lets other machines connect). The dictionary loads in under a second in a Release build.

To reproduce the benchmarks: `python bench/fetch_corpus.py`, then `./build/jss_bench` and `python bench/plot.py` (needs matplotlib). Details in `bench/README.md`.

## API

```
GET /api/search?q=日本語学校&mode=prefix|predictive&limit=50
GET /api/health
```

The response lists the matches (`key`, `length`, `entries` with kanji forms, readings, pitch and senses), the trie walk (`path`, `walk_stopped`), and `total`. Katakana queries that find nothing are retried in hiragana. The full format is in `server/api.hpp`.

## Layout

| Folder | Contents |
| --- | --- |
| `core/` | C++ library: UTF-8, trie, dictionary loader, ranking, baselines |
| `cli/` | `jss lookup <word>` |
| `server/` | `jss_server` and the JSON API |
| `web/` | The page (HTML, CSS, plain JS) |
| `tests/` | Unit tests (Catch2) |
| `bench/` | Benchmark harness, corpus script, plot script |
| `data/` | Dictionary preprocessing; sources and licenses |
| `docs/` | Plans, AI usage log, figures |

## Data credits

Uses [JMdict](https://www.edrdg.org/jmdict/j_jmdict.html) (CC BY-SA 4.0, Electronic Dictionary Research and Development Group) and pitch accent data from [Kanjium](https://github.com/mifunetoshiro/kanjium). See `data/README.md`. The benchmark text is seven public-domain novels by Natsume Sōseki from [Aozora Bunko](https://www.aozora.gr.jp/).
