# Data

Dictionary files are not committed. `build_dict.py` downloads the sources into `data/raw/` and writes `data/dict.tsv`, which the C++ loader reads. It needs only the Python standard library.

```sh
python data/build_dict.py            # uses data/raw/ if the files are already there
python data/build_dict.py --refresh  # downloads the sources again
python data/test_build_dict.py       # tests; no network or raw data needed
```

The script prints statistics, including the longest key in code points (Kmax in the benchmarks). The same inputs always give a byte-identical file.

## `dict.tsv` format

UTF-8 with `\n` line endings. Each line holds one fact, with fields separated by tabs. The first line is a `#` comment naming the JMdict date and the SHA-256 of both sources.

```
E	1358280
K	食べる	ichi1,news2,nf25
K	喰べる	
R	たべる	ichi1,news2,nf25	*	2
S	v1,vt	to eat
S	v1,vt	to live on (e.g. a salary)	to live off	to subsist on
```

| Line | Fields |
| --- | --- |
| `E` | Entry id (JMdict `ent_seq`). Starts a new entry; the lines after it belong to that entry |
| `K` | Kanji form, priority tags |
| `R` | Reading, priority tags, kanji forms it applies to, pitch |
| `S` | Parts of speech, then one field per gloss |

Notes on the fields:

- List fields are comma-joined and may be empty. Glosses are not lists and may contain commas. The script stops with an error if a value would break this format.
- Priority tags are per form, because spellings of a word can differ in frequency.
- The kanji forms field is `*` for all forms, a list for only those forms, or empty when the reading never goes with a kanji form (`re_nokanji`).
- Pitch values are downstep positions, so `0,3` means two accepted patterns. Empty means no data.
- Every sense has its parts of speech filled in, even where JMdict leaves them to carry over from the previous sense.

## Approximations and what is left out

- **Pitch is stored per reading, not per spelling.** A reading takes its pitch from the first allowed spelling that has data, or else from the kana alone. Spellings of one reading disagree for about 0.6% of readings (2026-10-31 data), often only in order or in having extra values.
- **Kanjium's part-of-speech labels on accents are dropped:** `(副)0,(名)3` becomes `0,3`.
- **Left out of the file for now:** `stagk`/`stagr` (senses restricted to certain forms), cross-references, `misc`, `field`, `dial`, and the `ke_inf`/`re_inf` form notes. Search-only and irregular spellings are therefore not marked, and they get inserted as keys like any other form.

## Sources and licenses

- [JMdict](https://www.edrdg.org/jmdict/j_jmdict.html), the official `JMdict_e.gz` XML, CC BY-SA 4.0, © Electronic Dictionary Research and Development Group. Used under the [EDRDG licence](https://www.edrdg.org/edrdg/licence.html), which requires a web dictionary to show the acknowledgement on each screen (a footer is enough), so the page in `web/` needs one.
- Pitch accent data from [Kanjium](https://github.com/mifunetoshiro/kanjium) (`accents.txt`), CC BY-SA 4.0. Pitch accent notation provided by Uros O.

The project reads the XML rather than the jmdict-simplified JSON because that format reduces the priority tags (`news1`, `ichi1`, `nf01`...) to one `common` boolean, and the cost model needs the tags.
