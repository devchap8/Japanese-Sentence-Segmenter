# AI usage log

The course requires a report section on AI use: where AI tools were used, what they got wrong, and what was fixed or rejected. Add an entry each time, while it is fresh.

| Date | Tool | What I asked for | What I used | What was wrong / what I changed or rejected |
| --- | --- | --- | --- | --- |
| 2026-10-29 | Claude | Project plan from the course rubric | Architecture, baselines, evaluation plan, timeline | |
| 2026-10-29 | Claude | Repo skeleton: CMake, Catch2, CI, README | Build setup and folder layout | |
| 2026-10-31 | Claude | Move plan and context into the repo for Claude Code CLI | `docs/PLAN.md`, `CLAUDE.md` | |
| 2026-10-31 | Claude | Data script: JMdict + Kanjium pitch data into one dictionary file, with tests | `data/build_dict.py`, `data/test_build_dict.py`, format in `data/README.md` | The AI-written plan chose jmdict-simplified, which drops the priority tags the cost model needs; switched to the JMdict XML. First version crashed printing Japanese on the Windows console and reported a misleading 16.4% unmatched pitch keys (really 0.7%); both fixed. |
| 2026-10-31 | Claude | UTF-8 decoder and encoder with tests | `core/src/utf8.cpp`, `core/include/jss/utf8.hpp`, `tests/test_utf8.cpp` | |
| 2026-11-01 | Claude | Trie tests, including the randomized oracle, to test my implementation against | `tests/test_trie.cpp`; the trie itself is mine | Checked against deliberately broken tries; the first version missed reads past the end of the text, so a test was added. |
| 2026-11-05 | Claude | (none) | running test environment | Claude used a test name that ctest can't run on Windows (japanese characters get mangled), change to romaji. |
| 2026-11-07 | Claude | Backup plan for the `dictionary` branch: trie-backed lookup page | `docs/PLAN-dictionary.md` | |
| 2026-11-10 | Claude | `dict.tsv` loader and its tests | `core/src/dictionary.cpp`, `core/include/jss/dictionary.hpp`, `tests/test_dictionary.cpp`, `tests/data/mini_dict.tsv` (real entries copied from `dict.tsv`) | |
| 2026-11-11 | Claude | `jss lookup <word>` CLI command; removed the `version.cpp` placeholder | `cli/main.cpp`, CMake changes | |
| 2026-11-13 | Claude | Predictive-search tests (random-dictionary oracle), then the subtree DFS in `Trie::predictive_search` | `tests/test_predictive.cpp`; I wrote the interface and the walk down to the prefix node, Claude wrote the explicit-stack DFS | Tests were checked against three deliberately broken implementations first. |
| 2026-11-13 | Claude | Ranking by JMdict priority tags, its tests, and ranked output in `jss lookup` | `core/src/ranking.cpp`, `core/include/jss/ranking.hpp`, `tests/test_ranking.cpp`, `cli/main.cpp` | The plan's "common" list left out `spec2`; used JMdict's own definition (news1, ichi1, spec1, spec2, gai1). |
| 2026-11-13 | Claude | Baselines (hash index, sorted array + binary search) with the same interface as the trie, and the equivalence test | `core/src/baselines.cpp`, `core/include/jss/baselines.hpp`, `tests/test_baselines.cpp` | First draft of `SortedArrayIndex::build` had a half-finished workaround for inserts after build; replaced with "build once, insert after throws". A deliberately broken sorted array passed every test, because `key[key.size()]` reads as U+0000; added a test with NUL characters, which catches it. |
