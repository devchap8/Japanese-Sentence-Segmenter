# Backup Plan: Trie-Backed Japanese Dictionary (`dictionary` branch)

Written Nov 7, 2026. This is the fallback version of the project. The full segmenter plan stays in `docs/PLAN.md` and continues on `main` if there is time.

## Summary

A web page where the user types Japanese and sees every JMdict word that the input starts with, found by the trie's common-prefix search in one walk. Typing 日本語学校 lists 日, 日本, 日本語 and 日本語学校, longest first, each with reading, pitch accent, part of speech and glosses.

The trie stays the graded data structure. What gets cut from the full plan is everything built *on top of* it: the lattice, cost model, Viterbi, deinflector, and the gold-set accuracy evaluation. The data-structure baselines and plots, which carry most of the grade, stay as they are.

| Rubric requirement | How this version meets it |
| --- | --- |
| Advanced data structure, written by the student | Pointer trie with common-prefix search and predictive (starts-with) search; double-array trie if the trie alone doesn't count as "advanced" (see Risks) |
| Core in C++ | Trie, dictionary loader, baselines, and HTTP server in C++17; Python only for data prep and plots; JS only for the page |
| Baseline + time vs. input size plots | Trie vs. `unordered_set` brute force vs. sorted array + binary search, all answering the same common-prefix query |
| Tests of the structure alone | Existing `tests/test_trie.cpp` (randomized oracle); add oracle tests for predictive search and an equivalence test across all structures |
| Repo, commits, README, `REPORT.md` | Same as before |
| Showcase demo | A live search page, plus a "trie path" view that shows the walk |

## What already exists

- `data/build_dict.py` → `data/dict.tsv` (format in `data/README.md`).
- UTF-8 decode/encode (`core/src/utf8.cpp`).
- `jss::Trie` with `insert`, `find`, `common_prefix_search`, passing `tests/test_trie.cpp`.

## Architecture

```
dict.tsv ──► DictLoader ──► vector<Entry>        (entry store, indexed 0..N-1)
                     └────► Trie  (key → entry indices)

browser ──GET /api/search?q=日本語学校──► jss_server ──► Trie::common_prefix_search(q, 0)
        ◄──────────── JSON: matches, longest first, with entries ─────────┘
```

**Recommendation: a small C++ server (cpp-httplib), not WebAssembly.** For a backup plan, lower risk beats offline hosting. The server loads the dictionary once at startup, serves the static page from `web/`, and answers one JSON endpoint. It runs on your laptop at the showcase. WASM would mean shipping a 23 MB dictionary to the browser and learning Emscripten; skip it unless everything else is done.

New code layout (additions only):

- `core/include/jss/dictionary.hpp`, `core/src/dictionary.cpp`: `Entry` struct and the `dict.tsv` loader.
- `core/include/jss/baselines.hpp`, `core/src/baselines.cpp`: hash-set and sorted-array versions of `common_prefix_search`.
- `server/main.cpp`: cpp-httplib server and JSON output. New CMake target `jss_server`.
- `bench/`: C++ timing harness (`bench/bench.cpp`) and `bench/plot.py`.
- `web/`: `index.html`, `app.js`, `style.css`. Plain JS, no build step.

## Core pieces

### 1. Dictionary loader

Parse `dict.tsv` into:

```cpp
struct Reading { std::string text; std::vector<std::string> priority;
                 std::vector<std::string> applies_to; std::vector<int> pitch; };
struct Sense   { std::vector<std::string> pos; std::vector<std::string> glosses; };
struct Entry   { int jmdict_id; std::vector<std::string> kanji;  // with priority per form
                 std::vector<Reading> readings; std::vector<Sense> senses; };
```

- The trie stores **indices into `vector<Entry>`**, not JMdict ids, so a lookup is one array access.
- Insert every kanji form and every reading as a key. Don't insert the same (key, entry) pair twice: the current `insert` would push a duplicate id.
- Skip the `#` header line; check that a file with an unknown line type fails loudly.
- Test on a tiny fixture file in `tests/data/` (3 to 5 entries, including a homograph like はし and a `re_nokanji` reading).

### 2. Predictive search

`common_prefix_search` answers "which words is my input a prefix *of*?" in reverse: which words are prefixes of the input. A dictionary search box also wants the other direction, "which words *start with* what I typed" (たべ → たべる, たべもの…). That is a second classic trie operation:

- Walk down to the node for the query (like `find`), then DFS the subtree, collecting ids until a limit `k` is hit.
- Complexity: O(m · log σ) to reach the node + O(nodes visited), bounded by `k`. Good material for the Analysis section, because hash sets and plain sorted arrays can't do this cheaply (sorted arrays can: a range from `lower_bound`, which is worth saying in the report).
- Oracle test: random dictionaries, random queries, compare with a scan over all keys using `starts_with`.

### 3. Ranking

Sort common-prefix matches longest first. Within one key, sort entries by a priority score from the form's tags (`ichi1`/`news1`/`spec1`/`gai1` = common; `nfXX` by number; no tags last). Predictive results sort by priority, then by length. Write this as a small free function, so it can be tested separately from the trie.

### 4. Input normalization

Showcase visitors can't type Japanese. Plan for it:

- Romaji → kana in the browser with the `wanakana` library (typing `taberu` searches たべる).
- Example buttons with prepared queries (日本語学校, すもももももも, はしる).
- Katakana → hiragana fallback on the server if a katakana query has no matches.

## Baselines and plots (the required comparison)

Every structure implements the same query: all dictionary keys that are prefixes of `text[i..]`.

| Structure | Query at position i | Cost per position |
| --- | --- | --- |
| `unordered_set<u32string>` | Hash every `text[i..i+k)` for k = 1..Kmax | O(Kmax²) |
| Sorted `vector<u32string>` + binary search | Binary-search every `text[i..i+k)` | O(Kmax · k · log D) |
| Pointer trie | One walk, stops at the first missing child | O(L · log σ), L usually < 10 |
| Double-array trie (if built) | One walk, O(1) per step | O(L) |

A single lookup is too fast to time, so the benchmark runs the query at **every position of a long text** (Tatoeba sentences or a novel from Aozora Bunko, concatenated). That is exactly what a reader tool does when the user hovers over each character, and it gives the time vs. input size plot.

Plots (median of 5 runs, `bench/plot.py` with matplotlib):

1. Time vs. text length, 1K to 1M characters, one line per structure.
2. Time vs. dictionary size, 10% to 100% of keys, at a fixed text length.
3. Memory and build time per structure (bar chart or table). The pointer trie will be large (a `Node` holds two vectors, and there are likely over a million nodes). That is an honest finding and the motivation for the double-array trie.
4. Predictive search: time vs. number of results returned (trie DFS vs. sorted-array range vs. hash-set full scan).

Equivalence test: on random dictionaries and texts, all structures must return identical results at every position.

## Server and frontend

**Endpoint** (one is enough):

```
GET /api/search?q=<utf8>&mode=prefix|predictive&limit=50
→ { "query": "日本語学校", "mode": "prefix",
    "path": [ {"char":"日","terminal":true}, {"char":"本","terminal":true}, ... ],
    "matches": [ { "length": 5, "key": "日本語学校", "entries": [ {...} ] }, ... ] }
```

Use cpp-httplib and nlohmann/json through `FetchContent`, both pinned to a release tag. `path` lists the nodes the walk visited, which feeds the trie view below.

**Page** (Claude can write most of this; it isn't graded as core):

- Search box with a mode toggle (Prefix matches / Starts with) and example buttons.
- Result cards: headword with the matched part highlighted, reading with furigana, pitch accent as a small SVG line or the Migaku colors from the main plan, part-of-speech chips, numbered glosses, a "common" badge.
- **Trie path view** (the showcase explainer, replacing the lattice view): the query's characters as a chain of nodes, with terminal nodes filled in and the matched words branching off them. It shows in one picture why one walk finds every match and why it stops early.
- Footer with the JMdict/EDRDG and Kanjium acknowledgement (the licence requires it on each screen).

## Testing

| Component | Tests |
| --- | --- |
| Trie | Existing tests, plus predictive-search oracle |
| Loader | Fixture file: entry counts, homographs share a key, `re_nokanji` handling, malformed line errors |
| Baselines | Equivalence with the trie on random dictionaries, at every position |
| Ranking | Hand cases: longest first, common before uncommon |
| Double-array trie | Equivalence with the pointer trie (if built) |
| Server | One test that calls the JSON builder directly (no network) and checks escaping of `"` and `\` in glosses |

## Milestones

Today is Nov 7. Graded core is still due before the trip.

| Dates | Deliverables |
| --- | --- |
| Nov 7 – 10 | Dictionary loader + fixture tests; `jss lookup <word>` in the CLI prints matches. Delete `core/src/version.cpp`. Email the instructor about the pivot and the "advanced structure" question (Risks) |
| Nov 11 – 13 | Predictive search + oracle test; ranking; hash-set and sorted-array baselines + equivalence test |
| Nov 14 – 16 | Benchmark harness, text corpus, plots 1–4 |
| Nov 17 – 19 | `jss_server` + a bare-bones page working end to end. **Merge `dictionary` into `main`** so the default branch has a working, graded project before the trip |
| Nov 20 | `REPORT.md` draft: Problem, Design, Analysis, Evaluation. Everything graded is done |
| Nov 21 – Dec 5 | Japan trip. Optional: start the double-array trie, report edits |
| Dec 6 – 11 | Page polish: result cards, pitch display, romaji input, trie path view. Double-array trie and its plot line if not done. Then, only if all of this is finished, resume segmenter work from `docs/PLAN.md` |
| Dec 12 – 16 | Feature freeze; README, `REPORT.md`, demo recording, rehearsal |
| Dec 17 | Repo deadline, 11:59 p.m. |
| Dec 18 | Showcase; final `REPORT.md` due |

**Cut list, in order:** trie path view, katakana fallback, pitch display, predictive-search plot, double-array trie (only if the instructor confirms the trie counts). Never cut: baselines, plots 1–3, oracle and equivalence tests.

## Risks

- **Does a plain trie count as "advanced, not covered in class"?** CSC 212 may cover tries. If it did, the double-array trie becomes the graded structure and moves before the trip (Nov 14–17), pushing the plots to Nov 18–19. Ask the instructor this week; this decides the schedule.
- **Scope looks small next to the original proposal.** Say so in the report: the segmenter was scoped down to a dictionary, and the benchmarks and tests are the same depth as planned. If segmenter work resumes on `main` in December, the report can include it as an extension.
- **Memory.** About 500k keys may make the pointer trie several hundred MB if node vectors over-allocate. Measure it early (Nov 10); `shrink_to_fit` after building, or switch to the double-array trie sooner.
- **Server startup time.** Parsing 23 MB of TSV at every launch should take a few seconds; acceptable. If not, add a binary cache file later (`data/*.bin` is already git-ignored).

## How this fits back into `main`

Everything on this branch is something the segmenter needs anyway: the loader, entry store, baselines, benchmarks, and the server. Merging `dictionary` into `main` loses nothing. The segmenter then adds the lattice, Viterbi and deinflector on top, and the dictionary page becomes the segmenter's popup.

## `REPORT.md` outline

1. **Problem:** learners need to look up words in unspaced Japanese text; a reader tool needs every dictionary word starting at a position, fast.
2. **Design:** the trie (node layout, sorted child vectors, ids into the entry store), common-prefix and predictive search, the double-array trie if built, the server and page.
3. **Analysis:** the complexity table above, plus memory.
4. **Evaluation:** plots 1–4 and what they show; correctness evidence from the oracle tests.
5. **AI usage:** from `docs/ai-log.md`, including this pivot plan.
6. **Contributions:** solo; commit history.

Showcase script (about 5 minutes): type a word and show the matches; switch to the trie path view to explain the walk; show plot 1 (trie vs. baselines); take questions on the code.
