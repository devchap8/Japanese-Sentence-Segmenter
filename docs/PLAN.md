# Japanese Word Splitter: CSC 212 Final Project Plan

Written Oct 29, 2026; exported from the planning doc on Oct 31, 2026. Rubric: https://homepage.cs.uri.edu/~malvarez/teaching/csc-212/projects/

## Summary and rubric fit

Build a C++ Japanese word segmenter: a JMDict trie finds every dictionary word in the text, Viterbi picks the cheapest path through that word lattice, and a browser page shows the result with pitch-accent underlines and a Yomitan-style popup. The graded core is the trie (the advanced data structure) measured against hash-set and sorted-array baselines, plus Viterbi measured against greedy longest match.

A naming note: the project splits sentences into **words** (word segmentation, 形態素解析-style), not text into sentences. Splitting on 。！？ is a one-line pre-step. Calling it a "word segmenter" in the proposal and report avoids a grader reading the title literally.

| Rubric requirement | How this plan meets it |
| --- | --- |
| Advanced data structure, not covered in class, written by the team | Trie with common-prefix search, then a double-array trie (the structure MeCab uses) as the advanced version |
| Core in C++ | Trie, deinflector, lattice, cost model and Viterbi in one C++17 library; Python only for data prep and plots, JS only for the page |
| Baseline + time vs. input size plots | Trie vs. brute-force hash lookups vs. sorted array + binary search; plots of time vs. text length and vs. dictionary size, plus memory |
| Tests of the structure on its own | Trie checked against a brute-force oracle on random dictionaries; Viterbi checked against exhaustive path enumeration |
| Repo with steady commits, tests, benchmark scripts, README | Repo created in week 1, CI on every push, `bench/` scripts that regenerate every plot |
| `REPORT.md`, 4 to 6 pages | Problem, Design, Analysis, Evaluation, AI usage, Contributions (see the last section) |
| Showcase live demo, numbers | Offline demo in the browser, lattice view to explain Viterbi, one page of plots |

Settled: proposal submitted, solo work approved by the instructor, repo created.

## Architecture

Recommendation: compile the C++ library to WebAssembly with Emscripten and run everything in the browser. The showcase demo then needs no server or Wi-Fi, and the same library also builds natively for tests and benchmarks.

| Option | Pros | Cons |
| --- | --- | --- |
| **WebAssembly (recommended)** | No backend; works offline at the showcase table; one static site on GitHub Pages | Dictionary file must download to the browser; Emscripten toolchain to learn; harder to debug |
| Small C++ HTTP server (cpp-httplib, one header) | Native speed and debugging; dictionary stays on disk | Laptop must run the server during the demo; no free hosting |

Both options call the same library, so the choice is reversible. If the dictionary is too large to load in the browser or Emscripten fights back, switch to the server.

Layout of the code:

- `core/` (C++17 library): UTF-8 decoding, trie, double-array trie, dictionary loader, deinflector, lattice builder, cost model, Viterbi, greedy baseline. No I/O beyond reading the dictionary file.
- `cli/`: a command-line tool that segments stdin. Used by the eval and benchmark scripts.
- `wasm/`: Emscripten bindings exposing `segment(text) -> JSON` and `lookup(entryId) -> JSON`.
- `tests/`: Catch2, run by CMake's `ctest` and by GitHub Actions on every push.
- `bench/`: C++ timing harness plus a Python script that draws the plots.
- `data/`: a Python script that turns JMDict and the pitch-accent data into one compact file the C++ loader reads quickly.
- `web/`: the page (plain TypeScript and Vite is enough; no framework needed).

Data flow for one request: text from the page → split on 。！？ and newlines → per sentence, build the lattice with the trie and deinflector → Viterbi → JSON list of tokens (surface, start, end, entry ids, deinflection chain, pitch pattern) plus, optionally, the full lattice for the visualizer.

## Dictionary data and trie design

The trie's one essential operation is **common-prefix search**: given the text and a start position, return every dictionary word that begins there, in a single walk. That walk is what makes the lattice cheap to build, and it is the operation the baselines are timed against.

**Data sources** (all free; credit them in the README):

- JMDict, using the official `JMdict_e.gz` XML. The [jmdict-simplified](https://github.com/scriptin/jmdict-simplified) JSON release was the first choice, but it reduces the priority tags below to one `common` boolean, which the cost model can't use. License CC BY-SA 4.0.
- Frequency: JMDict's priority tags (`news1/2`, `ichi1/2`, `spec1/2`, `gai1/2`, `nf01`–`nf48`) are the base. They are coarse and missing on most entries, so consider adding a corpus frequency list (the ones Yomitan users install, such as JPDB or BCCWJ-based lists) as a stretch.
- Pitch accent: the Kanjium accent list (word, reading, downstep position).

**Preprocessing (Python, in `data/`)** writes one compact file: for each entry an id, kanji forms, kana readings, part-of-speech tags, priority tags, pitch pattern, and glosses. Rubric note: Python is fine here because this is surrounding I/O, not the core structure.

**Keys.** Insert every kanji form and every kana reading, so たべる and 食べる both match. Each terminal node stores a small list of entry ids, because many words share a spelling (はし has bridge, chopsticks, edge). Key on Unicode code points, not bytes: decode UTF-8 to `char32_t` once per input.

**Version 1: pointer trie.** Kanji give thousands of possible children per node, so a fixed array per node is out. Store children as a sorted `vector<pair<char32_t, int>>` searched with binary search (compact, cache-friendly) or an `unordered_map` (simpler). Nodes live in one `vector` and refer to each other by index, which keeps memory measurable and avoids per-node allocations.

**Version 2: double-array trie** (Aoe's double array, as in MeCab's Darts library). Two integer arrays `base` and `check`; moving from state `s` on symbol `c` is `t = base[s] + code(c)`, valid if `check[t] == s`. Each step is O(1) with no search, and the whole structure is two flat arrays. Map code points to a dense alphabet first (frequent kana and kanji get small codes) to keep the arrays small. Build it from the finished pointer trie, so the two versions can be tested against each other.

**Operations and complexity** (L = length of the longest dictionary match at a position, usually under 10 characters; σ = children of a node):

| Operation | Pointer trie | Double-array trie |
| --- | --- | --- |
| `insert(word, id)` | O(len · log σ) | built offline from the pointer trie |
| `find(word)` | O(len · log σ) | O(len) |
| `commonPrefixSearch(text, i)` | O(L · log σ) | O(L) |
| Space | O(total characters of all keys) nodes | two int arrays, a little larger than the node count |

These are the numbers for the Analysis section of the report, and the benchmarks check them.

## Lattice, cost model and Viterbi

The segmenter is a shortest path over a DAG: positions 0..n sit between characters, each candidate word from i to j is a node, and the answer is the cheapest left-to-right path from position 0 to n.

**Define "word" first.** MeCab and Sudachi split 食べました into 食べ | まし | た. A learner tool wants 食べました as one clickable unit that looks up 食べる (that is how Yomitan and Migaku behave). Pick the learner unit: one token = a dictionary word plus its inflection. Particles stay separate tokens. Write this rule down early, because the test set has to follow it.

**1. Lattice construction.** For each position i:

1. Run `commonPrefixSearch(text, i)` and add a node for every match and every entry id it carries.
2. **Deinflection:** for each end j where `text[j-1]` is kana, pass `text[i..j)` to the deinflector. It applies suffix-rewrite rules in a loop, Yomitan style (かった → い for i-adjectives; ました → ます → る for ichidan; った → う/つ/る for godan), and looks each candidate lemma up with `find`. Keep a candidate only if the entry's part of speech matches the rule (v1, v5k, adj-i, vs…). Add a node that records the lemma and the rule chain, such as "past ← polite".
3. **Unknown words:** if nothing starts at i, add one node per run of the same script (a katakana run, a number, Latin letters, a kanji run) with a high per-character cost. Also add a single-character node at every position with a very high cost, so a path from 0 to n always exists.

**2. Costs.** Lower is better.

- **Word cost** from frequency: map each priority bucket to a cost (for example `news1`/`ichi1`/`spec1` low, `nf01`–`nf48` rising with the number, no tag high). With a corpus frequency list, use −log(relative frequency).
- **Length and shape adjustments:** a small penalty per token so the path prefers fewer, longer words; an extra penalty on single-kanji nouns and single-kana non-particles, which cause most over-splitting.
- **Deinflection cost:** a small cost per rule applied, so a direct match beats a long rule chain.
- **Connection cost** between adjacent tokens from a hand-written table over coarse part-of-speech classes (noun, verb, i-adjective, particle, auxiliary, adverb, prefix, suffix, symbol, unknown). Examples: particle → particle high (the はが example), noun → particle low, prefix → noun low, verb → noun moderate. This is the bigram model MeCab uses, with a 10×10 table instead of a learned one.

**3. Viterbi.** Because connection costs depend on the previous token's class, the dynamic program is over nodes, not positions:

```
best(v) = wordCost(v) + min over u with end(u) == start(v) of ( best(u) + conn(class(u), class(v)) )
```

Process nodes in order of start position (a topological order of the DAG), keep a back-pointer per node, and backtrack from the cheapest node ending at n. With a BOS node at 0 and an EOS node at n, runtime is O(Σ over positions of (nodes ending there) × (nodes starting there)), which is linear in n in practice because a position has a bounded number of candidates. Ties break on the earlier node id, so output is deterministic.

**4. Tuning.** Hand-tune the cost table on a development split of the test set and freeze it before measuring on the held-out split. Stretch goal: learn the weights with a structured perceptron on the silver data, which would be a strong extra-credit story.

**Stretch: N-best.** Keeping the top 2 or 3 paths per node lets the page show "other readings" for ambiguous spots such as ここではきものをぬいでください.

## Baselines and comparisons

The rubric's baseline is about the **data structure**, with time vs. input size plots as in Lab 1. Segmentation quality is a second, separate comparison. Plan both, and keep them apart in the report.

**Comparison A: dictionary lookup structure (the rubric's required baseline).** Every structure answers the same question: all dictionary words starting at position i.

| Structure | How it finds words at position i | Expected cost per position |
| --- | --- | --- |
| Brute force: `unordered_set<u32string>` | Build and hash every substring `text[i..i+k)` for k = 1..Kmax (Kmax = longest key, about 20+) | O(Kmax²): Kmax substrings, each hashed in O(k) |
| Sorted array + binary search | Binary-search each substring `text[i..i+k)` | O(Kmax · log D · k) for D keys |
| Pointer trie | One walk, stops when no child matches | O(L · log σ), L usually < 10 |
| Double-array trie | One walk, O(1) per step | O(L) |

The trie wins because it stops as soon as the prefix leaves the dictionary, and it shares work across all lengths. The baselines redo the work for each length and cannot stop early (the sorted array can, with care, but the simple version doesn't).

Plots to produce (C++ timer, median of 5 runs, Python matplotlib):

1. Lattice-build time vs. text length, 1K to 1M characters (real text, concatenated), one line per structure. Expect four straight lines with very different slopes.
2. Time vs. dictionary size, 10% to 100% of JMDict keys, at fixed text length. Shows the log D term in the sorted array and the flat trie.
3. Memory and build time per structure, as a small table or bar chart.
4. Viterbi time vs. text length, to show the full pipeline is linear.

**Comparison B: segmentation algorithm.** Same dictionary and lattice, different path choice:

| System | Purpose |
| --- | --- |
| Greedy longest match (trie-based, left to right) | The obvious algorithm; the baseline for accuracy |
| Viterbi, word costs only | Shows what frequency alone buys |
| Viterbi + connection costs | Shows what the particle and part-of-speech table buys |
| Viterbi + connection costs + deinflection (full system) | The final result |
| MeCab (IPAdic or UniDic) and Sudachi | Reference points from real tools, run through their Python packages (fugashi, SudachiPy) |

The middle rows are an ablation: each row adds one idea, so the report can say how much each one is worth. MeCab and Sudachi are references, not baselines to beat. They use a different word unit, so compare against them only after converting their output (next section).

## Evaluation: test set and metrics

Use two test sets: a small hand-checked **gold** set that the accuracy claims rest on, and a large automatic **silver** set for scale and timing.

**Source text.** Tatoeba's Japanese sentences (CC BY 2.0 FR, short everyday sentences, easy to check by hand). Add a few paragraphs of news or graded-reader text so long sentences and katakana loanwords are covered.

**Silver set (about 5,000 sentences).** Run Sudachi (mode C, its longest unit) and convert to the learner unit with a script: merge auxiliary verbs and inflection suffixes into the preceding verb or adjective, keep particles separate. Mode C is chosen because it is closest to the learner unit; the conversion rule goes in the report.

**Gold set (about 150 sentences for the solo schedule).** Start from the silver output and correct it by hand against the unit rule. Split it 50/50 into a dev half (for tuning costs) and a test half (touched only for final numbers). Add 30 or so hand-picked hard cases: はが-style particle runs, すもももももももものうち, all-hiragana sentences, long conjugation chains, katakana compounds, numbers and Latin text.

**Metrics** (all computed by one Python script over the CLI's output):

| Metric | Definition | Why |
| --- | --- | --- |
| Token precision / recall / F1 | A token counts as correct only if both its start and end match the gold | Standard for word segmentation |
| Boundary F1 | Precision and recall over the set of split points | More forgiving; shows near-misses |
| Sentence accuracy | Share of sentences segmented exactly right | Easy for a showcase audience to understand |
| Lemma accuracy | Share of correct tokens whose looked-up dictionary form is right | Checks the deinflector and the popup |
| Throughput | Characters per second, end to end | Ties back to Comparison A |

Finish with a short error analysis: sample 30 errors from the test half, sort them into kinds (over-split, under-split, wrong deinflection, unknown word), and report the counts. Graders like seeing *why* it fails.

## Testing strategy

The strongest tests compare each clever component with a slow, obviously correct version on many random inputs. That proves correctness rather than spot-checking it, and it answers the rubric's "tests that verify the structure's correctness on its own" directly.

| Component | Tests |
| --- | --- |
| UTF-8 decoding | Round-trip on all kana, common kanji, emoji (4-byte), and malformed bytes (must not crash) |
| Pointer trie | Unit tests for insert, find, prefix sharing, homographs, empty string. **Randomized oracle:** random dictionaries of 1 to 5,000 words over a small alphabet, random texts; `commonPrefixSearch` must equal a brute-force scan over every key, at every position |
| Double-array trie | Built from the pointer trie, then the same random queries must give identical results to the pointer trie |
| Deinflector | Table-driven: for each verb class (godan うくぐすつぬぶむる, ichidan, する, 来る, i-adjective) and each form (negative, past, te, polite, potential, passive, causative, volitional, conditional, plus chains like 食べさせられなかった), the expected lemma and rule chain. Irregulars such as 行った and いい→よかった |
| Lattice | A path from 0 to n exists for every input, including empty text, emoji, Latin, and text with no dictionary words. Every node's start < end and both lie within the text |
| Viterbi | **Exhaustive oracle:** on random small lattices (n ≤ 12, random costs), enumerate every path and check Viterbi's cost equals the minimum. Hand-built lattices with a known answer. Same input always gives the same output |
| Greedy baseline | Hand cases where greedy is known to fail, so the comparison is honest |
| End to end | A golden file of about 50 sentences and expected segmentations; a failing line is a regression |
| WASM bindings | One smoke test that loads the module in Node and segments a sentence |

Run everything with `ctest` and in GitHub Actions on every push.

## Frontend

The page has three views of the same result: the reading view for learners, a popup for one word, and a lattice view that explains the algorithm at the showcase.

**Reading view.** A text box; on submit, each token renders as a `<span>` with a colored underline and a small gap between tokens. Underline color follows the pitch-accent pattern, Migaku style:

| Pattern | Meaning | Underline |
| --- | --- | --- |
| Heiban (0) | No drop | Blue |
| Atamadaka (1) | Drop after the first mora | Red |
| Nakadaka (2..n−1) | Drop in the middle | Orange |
| Odaka (n) | Drop after the last mora, shows on the next particle | Green |
| Unknown / particle | No pitch data | Gray |

Pitch comes from the Kanjium data keyed by the lemma and reading; a conjugated verb uses its lemma's pattern as an approximation (say so in the report). Optional toggles: furigana with `<ruby>` tags, and hiding particles' underlines.

**Popup (Yomitan style).** Clicking a token opens a card anchored to it with: the headword with furigana, the reading with a pitch graph (a small SVG line with the downstep marked), the deinflection chain (食べられなかった ← 食べる: passive, negative, past), part of speech, numbered English glosses, frequency tag, and other entries with the same spelling. The popup data comes from `lookup(entryId)`, so the segment call stays small.

**Lattice view (the showcase explainer).** Characters across the top; below them, one bar per candidate word spanning its characters, stacked in rows like a Gantt chart. The chosen Viterbi path is highlighted, rejected candidates are gray, and hovering a bar shows its word cost and connection cost. For すもももももももものうち this makes the whole algorithm visible in one picture. A "greedy" toggle shows where greedy longest match goes wrong on the same sentence.

**Tech.** Plain TypeScript with Vite, the WASM module loaded once at startup, the dictionary file fetched and cached by the browser.

## Milestones

Solo project (instructor approved) with a Japan trip from Nov 21 to Dec 5. Everything graded (trie, baselines, Viterbi, tests, plots, report draft) lands by Nov 20. The 11 days after the trip (Dec 6 to 16) go to the web page, the final report, and rehearsal.

| Dates | Deliverables |
| --- | --- |
| Oct 29 – Nov 4 | Repo skeleton, CMake, CI, tests (done); data script producing the dictionary file; UTF-8 decoding; pointer trie with oracle tests |
| Nov 5 – 11 | Hash-set and sorted-array baselines; benchmark harness and the first time vs. text length plot; double-array trie with equivalence tests; greedy longest match |
| Nov 12 – 17 | Lattice builder with unknown-word fallback; Viterbi with the exhaustive oracle test; deinflector with table tests; connection-cost table |
| Nov 18 – 20 | Label a gold set of about 150 sentences; eval script; tune on the dev half; final plots and ablation; first full draft of `REPORT.md` (Problem, Design, Analysis, Evaluation). A command-line demo works. **Everything graded is done here.** |
| Nov 21 – Dec 5 | Japan trip. Nothing required. If there's time: a half-day WASM spike or report edits. Commit whatever you do, since steady history counts |
| Dec 6 – 11 | WASM build; reading view with pitch underlines; popup; lattice view |
| Dec 12 – 16 | Feature freeze; finish README and `REPORT.md`; record the demo video; rehearse the 5-minute demo |
| Dec 17 | Repo deadline, 11:59 p.m. |
| Dec 18 | Showcase; final repo and `REPORT.md` due 11:59 p.m. |

If the WASM build fights back after the trip, switch to the cpp-httplib server; it takes about a day and uses the same library.

**Cut list, solo edition.** If a week slips, drop these in order and say so in the report: learned weights (perceptron), N-best paths, the corpus frequency list, the silver set (keep the gold set), furigana toggle, the lattice view, then pitch underlines. Never cut the baselines, plots, or oracle tests; they carry the grade.

Working with AI as a solo developer: the rubric says every line must be explainable at the showcase, so write or rewrite the core (trie, double array, Viterbi, deinflector) yourself and use AI for review, test ideas, build setup and the frontend. Log each use in `docs/ai-log.md` as you go; commits stay under your name only.

## Report and showcase

The repo and report are 50% of the grade and the showcase is 40%, so the last week goes to both, not to new features.

**`REPORT.md` outline (4 to 6 pages):**

1. **Problem:** Japanese has no spaces; learners need word boundaries and dictionary forms to look words up. Built for learners using a Yomitan/Migaku-style reader.
2. **Design:** the trie and double-array trie (with a small figure of `base`/`check`), the lattice, the cost model, deinflection, Viterbi.
3. **Analysis:** the complexity table from the trie section, plus O(n) lattice build and Viterbi with the bounded-candidates argument, plus memory.
4. **Evaluation:** the four Comparison A plots, the Comparison B ablation table, and the error analysis.
5. **AI usage:** from `docs/ai-log.md`: what AI wrote, what it got wrong, and what was rejected.
6. **Contributions:** solo project; the commit history backs it up.

**Showcase demo script (about 5 minutes):**

1. Paste a sentence; the reading view appears with pitch underlines. Click a conjugated verb to show the popup and its deinflection chain.
2. Switch to the lattice view on すもももももももものうち; toggle greedy vs. Viterbi to show why the shortest path matters.
3. Show one page of numbers: the time vs. text length plot (trie vs. baselines) and the F1 table.
4. Be ready to explain any line of the code.

Have a fallback: a screen recording of the demo and the plots as images, in case the laptop or browser misbehaves at the table.
