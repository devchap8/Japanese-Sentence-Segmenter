# Japanese Sentence Segmenter

Devin's solo final project for CSC 212 (Data Structures, URI, Fall 2026). The full plan is in `docs/PLAN.md`; read it before starting any milestone. Rubric: https://homepage.cs.uri.edu/~malvarez/teaching/csc-212/projects/

## What it is

Word segmentation for Japanese. The repo name says "sentence", but the program splits sentences into words.

- JMdict loaded into a trie (then a double-array trie). Common-prefix search at each position builds a word lattice.
- A Yomitan-style deinflector adds conjugated forms as lattice nodes. Token unit = dictionary word plus its inflection (食べました is one token, lemma 食べる); particles are separate tokens.
- Costs: word cost from JMdict priority tags, a small per-token penalty, connection costs from a hand-written part-of-speech table (particle → particle is expensive). Unknown-word fallback so a path always exists.
- Viterbi over the lattice picks the cheapest path.
- Web page (planned: WebAssembly via Emscripten; cpp-httplib server as fallback): Migaku-style pitch-accent underlines, Yomitan-style popup, lattice view for the demo.

## Rubric essentials

- The core data structure is C++ written by Devin. Every line must be explainable at the showcase, so Devin writes the core algorithms (trie, double-array trie, Viterbi, deinflector). Claude explains, reviews, suggests tests, and helps with build setup, scripts, and the frontend. When asked for core code, prefer explaining the approach and reviewing Devin's version over writing it outright.
- Baseline comparison with time vs. input size plots: trie vs. `unordered_set` brute force vs. sorted array + binary search. Separately: greedy longest match vs. a Viterbi ablation on a hand-checked gold set (~150 Tatoeba sentences); token F1, boundary F1, sentence accuracy.
- Tests of the structure alone: trie vs. a brute-force oracle on random dictionaries; double-array trie vs. pointer trie; Viterbi vs. exhaustive path enumeration on small lattices.
- `REPORT.md` sections: Problem, Design, Analysis, Evaluation, AI usage, Contributions. Steady commit history is graded.

## Schedule

- Graded core (trie, baselines, Viterbi, tests, plots, report draft) done by 2026-11-20.
- Japan trip 2026-11-21 to 2026-12-05.
- Dec 6–11 web page; Dec 12–16 report and rehearsal.
- Repo deadline 2026-12-17 11:59 p.m.; showcase and `REPORT.md` 2026-12-18.

## Status

- Done: repo skeleton (CMake, Catch2 v3 via FetchContent, GitHub Actions on Ubuntu and macOS, README, `docs/ai-log.md`).
- Next (Oct 29 – Nov 4): data script in `data/` that turns jmdict-simplified JSON + Kanjium pitch data into one compact file; UTF-8 → `char32_t` decoding; pointer trie with randomized oracle tests.
- `core/src/version.cpp` is a placeholder; delete it once real code lands.

## Working rules

- Commits: plain messages under Devin's name. Never add `Co-Authored-By`, `Claude-Session`, "Generated with Claude Code", or any Claude attribution to commits or PRs.
- Log AI use in `docs/ai-log.md` as it happens (date, what was asked, what was used, what was wrong or rejected). The report's AI usage section comes from it.
- Keep `ctest` green; CI runs on every push.
- C++17, code in namespace `jss`, headers in `core/include/jss/`, sources in `core/src/`. Add new sources to `jss_core` and new tests to `jss_tests` in `CMakeLists.txt`.
- Data files are not committed (`data/raw/`, generated dictionary files are git-ignored).

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
echo "猫が好きです" | ./build/jss
```
