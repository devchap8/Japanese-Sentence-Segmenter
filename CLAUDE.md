# Japanese Sentence Segmenter: `dictionary` branch

Devin's solo final project for CSC 212 (Data Structures, URI, Fall 2026). Rubric: https://homepage.cs.uri.edu/~malvarez/teaching/csc-212/projects/

**This branch is the backup version of the project.** Its plan is `docs/PLAN-dictionary.md`; read it before starting any milestone. The full segmenter plan (`docs/PLAN.md`) continues on `main` only if there is time after this branch is done. Don't start segmenter work (lattice, Viterbi, deinflector, gold set) here.

## What it is

A trie-backed Japanese dictionary lookup page.

- JMdict loaded from `data/dict.tsv` into a `vector<Entry>`. Every kanji form and reading is inserted into a pointer trie, which stores indexes into that vector.
- The user types Japanese. Common-prefix search lists every word the input starts with, longest first (日本語学校 → 日, 日本, 日本語, 日本語学校). Predictive search lists words that start with the input. Results are ranked by JMdict priority tags.
- A cpp-httplib server (`jss_server`) loads the dictionary once and answers `GET /api/search?q=…&mode=prefix|predictive`. It also serves the static page in `web/` (plain JS, no build step).
- Page: result cards (reading, pitch, part of speech, glosses), romaji input via wanakana, example buttons, and a trie path view for the demo. The JMdict/EDRDG and Kanjium acknowledgement goes in the footer.
- A double-array trie is planned if the instructor says a plain trie doesn't count as "advanced, not covered in class"; otherwise it's a stretch goal.

## Rubric essentials

- The core data structure is C++ written by Devin. Every line must be explainable at the showcase, so Devin writes the core code (trie, predictive search, dictionary loader, ranking, baselines, double-array trie). Claude explains, reviews, suggests tests, and helps with build setup, the benchmark harness, plot scripts, the server glue, and the frontend. When asked for core code, prefer explaining the approach and reviewing Devin's version over writing it outright.
- Baseline comparison with time vs. input size plots: trie vs. `unordered_set` brute force vs. sorted array + binary search. All of them answer the same common-prefix query at every position of a long text. The plots are time vs. text length, time vs. dictionary size, memory and build time, and predictive-search time.
- Tests of the structure alone: trie vs. a brute-force oracle on random dictionaries, a predictive-search oracle, an equivalence test across all structures, loader tests on a fixture file, and the double-array trie vs. the pointer trie if it's built.
- `REPORT.md` sections: Problem, Design, Analysis, Evaluation, AI usage, Contributions. The report must say that the project was scoped down from the segmenter. Steady commit history is graded.

## Schedule

- Nov 7–10 loader + CLI lookup; Nov 11–13 predictive search, ranking, baselines; Nov 14–16 benchmarks and plots; Nov 17–19 server and a basic page, then merge `dictionary` into `main`; Nov 20 `REPORT.md` draft. Graded core done by 2026-11-20.
- Japan trip 2026-11-21 to 2026-12-05.
- Dec 6–11 page polish (and the double-array trie if not done); Dec 12–16 report and rehearsal.
- Repo deadline 2026-12-17 11:59 p.m.; showcase and `REPORT.md` 2026-12-18.

## Status

- Done: repo skeleton (CMake, Catch2 v3 via FetchContent, GitHub Actions on Ubuntu and macOS, README, `docs/ai-log.md`); `data/build_dict.py` (JMdict XML + Kanjium pitch data → `data/dict.tsv`, about 219k entries and 500k keys, format in `data/README.md`); UTF-8 decoding and encoding (written by Claude at Devin's request); pointer trie with `insert`, `find`, `common_prefix_search`, written by Devin and passing `tests/test_trie.cpp`.
- Done: dictionary loader `jss::load_dictionary` (`core/src/dictionary.cpp`, written by Claude at Devin's request; Devin's review pending), tests in `tests/test_dictionary.cpp`. It inserts each spelling once per entry. Full `dict.tsv` (Release, 2026-11-10): 218,995 entries, 499,678 keys, about 0.7 s to load, about 255 MB peak. Debug takes about 10 s. Hidden test `jss_tests "[real]"` loads the real file.
- Done: `jss lookup [--dict <path>] <word>...` (`cli/main.cpp`, written by Claude): common-prefix matches longest first, entries in file order until ranking lands. Reads the wide command line on Windows so Japanese arguments survive. Default dict path is baked in at build time.
- Done: `Trie::predictive_search(prefix, limit)`: walk to the prefix node (Devin), then a pre-order DFS with an explicit stack (Claude, at Devin's request), results in code point order, `limit` counts keys. Tests in `tests/test_predictive.cpp`.
- Done: ranking (`jss/ranking.hpp`, written by Claude at Devin's request; Devin's review pending): `priority_score` (common + nfXX band, lower is better), `rank_ids`, `rank_prefix_matches` (longest first), `rank_predictive_matches` (score, then length, then code point). Predictive top-k must search wider than k, rank, then cut. `jss lookup` prints ranked results. Tests in `tests/test_ranking.cpp`.
- Next: baselines (`unordered_set`, sorted array) and the equivalence test.
- Open question for the instructor: does a plain trie count as the advanced structure? If not, the double-array trie moves before the trip.

## Working rules

- Commits: plain messages under Devin's name. Never add `Co-Authored-By`, `Claude-Session`, "Generated with Claude Code", or any Claude attribution to commits or PRs.
- Log AI use in `docs/ai-log.md` as it happens (date, what was asked, what was used, what was wrong or rejected). The report's AI usage section comes from it.
- Keep `ctest` green; CI runs on every push.
- C++17, code in namespace `jss`, headers in `core/include/jss/`, sources in `core/src/`, server in `server/`, benchmarks in `bench/`. Add new sources to `jss_core` and new tests to `jss_tests` in `CMakeLists.txt`. Pin FetchContent dependencies (cpp-httplib, nlohmann/json) to release tags.
- Test names use ASCII only, because ctest on Windows mangles Japanese test names.
- Data files are not committed (`data/raw/` and generated dictionary files are git-ignored).

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/jss lookup 日本語学校
./build/jss_server               # planned; then open http://localhost:8080
```
