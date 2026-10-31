# AI usage log

The course requires a report section on AI use: where AI tools were used, what they got wrong, and what was fixed or rejected. Add an entry each time, while it is fresh.

| Date | Tool | What I asked for | What I used | What was wrong / what I changed or rejected |
| --- | --- | --- | --- | --- |
| 2026-10-29 | Claude | Project plan from the course rubric | Architecture, baselines, evaluation plan, timeline | |
| 2026-10-29 | Claude | Repo skeleton: CMake, Catch2, CI, README | Build setup and folder layout | |
| 2026-10-31 | Claude | Move plan and context into the repo for Claude Code CLI | `docs/PLAN.md`, `CLAUDE.md` | |
| 2026-10-31 | Claude | Data script: JMdict + Kanjium pitch data into one dictionary file, with tests | `data/build_dict.py`, `data/test_build_dict.py`, format in `data/README.md` | The AI-written plan chose jmdict-simplified, which drops the priority tags the cost model needs; switched to the JMdict XML. First version crashed printing Japanese on the Windows console and reported a misleading 16.4% unmatched pitch keys (really 0.7%); both fixed. |
| 2026-10-31 | Claude | UTF-8 decoder and encoder with tests | `core/src/utf8.cpp`, `core/include/jss/utf8.hpp`, `tests/test_utf8.cpp` | |
