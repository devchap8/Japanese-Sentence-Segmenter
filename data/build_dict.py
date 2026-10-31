#!/usr/bin/env python3
"""Turn JMdict and the Kanjium pitch accent list into data/dict.tsv.

Usage: python data/build_dict.py [--refresh]

The output format is described in data/README.md.
"""

import argparse
import gzip
import hashlib
import re
import sys
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

JMDICT_URL = "http://ftp.edrdg.org/pub/Nihongo/JMdict_e.gz"
KANJIUM_URL = (
    "https://raw.githubusercontent.com/mifunetoshiro/kanjium/"
    "master/data/source_files/raw/accents.txt"
)

DATA_DIR = Path(__file__).resolve().parent
RAW_DIR = DATA_DIR / "raw"
OUTPUT_PATH = DATA_DIR / "dict.tsv"

PREDEFINED_ENTITIES = {"amp", "lt", "gt", "quot", "apos"}
UNMATCHED_SAMPLE_SIZE = 10


def download(url, path, refresh):
    if path.exists() and not refresh:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    print(f"Downloading {url}")
    temp_path = path.with_name(path.name + ".part")
    with urllib.request.urlopen(url, timeout=120) as response:
        temp_path.write_bytes(response.read())
    temp_path.replace(path)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def prepare_xml(xml_text):
    """Remove the DTD and turn entities like &v1; into the bare code v1.

    Parsing with the DTD would expand &v1; to "Ichidan verb", and the segmenter
    wants the short codes.
    """
    without_dtd = re.sub(
        r"<!DOCTYPE.*?^\]>", "", xml_text, count=1, flags=re.DOTALL | re.MULTILINE
    )

    def replace_entity(match):
        name = match.group(1)
        return match.group(0) if name in PREDEFINED_ENTITIES else name

    return re.sub(r"&([A-Za-z0-9_-]+);", replace_entity, without_dtd)


def jmdict_date(xml_text):
    match = re.search(r"<!-- JMdict created: (\S+) -->", xml_text)
    return match.group(1) if match else "unknown"


def parse_jmdict(xml_text):
    """Return entries as plain dicts, in document order."""
    root = ET.fromstring(prepare_xml(xml_text))
    entries = []
    for entry in root.iter("entry"):
        kanji = [
            {
                "text": k_ele.findtext("keb"),
                "priorities": [e.text for e in k_ele.findall("ke_pri")],
            }
            for k_ele in entry.findall("k_ele")
        ]

        readings = []
        for r_ele in entry.findall("r_ele"):
            if r_ele.find("re_nokanji") is not None:
                restrictions = []
            else:
                restrictions = [e.text for e in r_ele.findall("re_restr")] or ["*"]
            readings.append(
                {
                    "text": r_ele.findtext("reb"),
                    "priorities": [e.text for e in r_ele.findall("re_pri")],
                    "restrictions": restrictions,
                }
            )

        # JMdict omits <pos> when a sense has the same parts of speech as the
        # sense before it.
        senses = []
        pos = []
        for sense in entry.findall("sense"):
            pos = [e.text for e in sense.findall("pos")] or pos
            senses.append(
                {"pos": pos, "glosses": [e.text for e in sense.findall("gloss")]}
            )

        entries.append(
            {
                "id": entry.findtext("ent_seq"),
                "kanji": kanji,
                "readings": readings,
                "senses": senses,
            }
        )
    return entries


def fold_kana(text):
    """Map katakana to hiragana so the two scripts compare equal."""
    return "".join(
        chr(ord(c) - 0x60) if "ァ" <= c <= "ヶ" else c for c in text
    )


def normalize_accent(accent):
    """Turn "(副)0,(名)3" into ["0", "3"], dropping part-of-speech labels."""
    values = re.findall(r"\d+", re.sub(r"\([^)]*\)", "", accent))
    return list(dict.fromkeys(values))


def parse_accents(text):
    """Return {(word, reading): [downstep positions]}, with kana folded.

    Kanjium leaves the reading empty when the word is already kana.
    """
    accents = {}
    for line in text.splitlines():
        fields = line.split("\t")
        if len(fields) != 3:
            continue
        word, reading, accent = fields
        key = (fold_kana(word), fold_kana(reading or word))
        values = accents.setdefault(key, [])
        values.extend(v for v in normalize_accent(accent) if v not in values)
    return accents


def find_pitch(entry, reading, accents, used_keys):
    """Pitch for a reading: first allowed kanji form with data, else the kana."""
    restrictions = reading["restrictions"]
    forms = [
        k["text"]
        for k in entry["kanji"]
        if restrictions == ["*"] or k["text"] in restrictions
    ]
    folded_reading = fold_kana(reading["text"])
    keys = [(fold_kana(form), folded_reading) for form in forms + [reading["text"]]]
    matches = [key for key in keys if key in accents]
    used_keys.update(matches)
    return accents[matches[0]] if matches else []


def checked(field, allow_comma=True):
    """Fail loudly on a value that would corrupt the line format."""
    if "\t" in field or "\n" in field or "\r" in field:
        raise ValueError(f"tab or newline in field: {field!r}")
    if not allow_comma and "," in field:
        raise ValueError(f"comma in field: {field!r}")
    return field


def joined(values):
    return ",".join(checked(v, allow_comma=False) for v in values)


def format_entry(entry, accents, used_keys):
    lines = [f"E\t{checked(entry['id'])}"]
    for kanji in entry["kanji"]:
        form = checked(kanji["text"], allow_comma=False)
        lines.append(f"K\t{form}\t{joined(kanji['priorities'])}")
    for reading in entry["readings"]:
        pitch = find_pitch(entry, reading, accents, used_keys)
        text = checked(reading["text"], allow_comma=False)
        lines.append(
            f"R\t{text}\t{joined(reading['priorities'])}"
            f"\t{joined(reading['restrictions'])}\t{joined(pitch)}"
        )
    for sense in entry["senses"]:
        fields = [joined(sense["pos"])] + [checked(g) for g in sense["glosses"]]
        lines.append("S\t" + "\t".join(fields))
    return lines


def build(xml_text, accents_text, header=""):
    """Return (output text, statistics)."""
    entries = parse_jmdict(xml_text)
    accents = parse_accents(accents_text)

    used_keys = set()
    lines = [header] if header else []
    for entry in entries:
        lines.extend(format_entry(entry, accents, used_keys))

    keys = set()
    entries_with_priority = 0
    priority_entries_with_pitch = 0
    readings_total = 0
    readings_with_pitch = 0
    for entry in entries:
        forms = entry["kanji"] + entry["readings"]
        keys.update(form["text"] for form in forms)
        has_priority = any(form["priorities"] for form in forms)
        has_pitch = False
        for reading in entry["readings"]:
            readings_total += 1
            if find_pitch(entry, reading, accents, set()):
                readings_with_pitch += 1
                has_pitch = True
        entries_with_priority += has_priority
        priority_entries_with_pitch += has_priority and has_pitch

    stats = {
        "entries": len(entries),
        "unique_keys": len(keys),
        "longest_key": max(map(len, keys), default=0),
        "entries_with_priority": entries_with_priority,
        "priority_entries_with_pitch": priority_entries_with_pitch,
        "readings": readings_total,
        "readings_with_pitch": readings_with_pitch,
        "accent_keys": len(accents),
        "unmatched_accent_keys": sorted(set(accents) - used_keys),
    }
    return "\n".join(lines) + "\n", stats


def percent(part, whole):
    return f"{100 * part / whole:.1f}%" if whole else "n/a"


def print_stats(stats, output_size):
    unmatched = stats["unmatched_accent_keys"]
    print(f"Entries:             {stats['entries']}")
    print(f"Unique keys:         {stats['unique_keys']}")
    print(f"Longest key:         {stats['longest_key']} code points")
    print(
        "Entries w/ priority: "
        f"{stats['entries_with_priority']} "
        f"({percent(stats['entries_with_priority'], stats['entries'])})"
    )
    print(
        "Priority w/ pitch:   "
        f"{stats['priority_entries_with_pitch']} of {stats['entries_with_priority']} "
        f"({percent(stats['priority_entries_with_pitch'], stats['entries_with_priority'])})"
    )
    print(
        "Readings w/ pitch:   "
        f"{stats['readings_with_pitch']} of {stats['readings']} "
        f"({percent(stats['readings_with_pitch'], stats['readings'])})"
    )
    print(
        "Kanjium unmatched:   "
        f"{len(unmatched)} of {stats['accent_keys']} "
        f"({percent(len(unmatched), stats['accent_keys'])})"
    )
    step = max(1, len(unmatched) // UNMATCHED_SAMPLE_SIZE)
    for word, reading in unmatched[::step][:UNMATCHED_SAMPLE_SIZE]:
        print(f"  e.g. {word} {reading}")
    print(f"Output size:         {output_size / 1_000_000:.1f} MB")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--refresh", action="store_true", help="download the sources again"
    )
    args = parser.parse_args(argv)

    # The Windows console defaults to a code page that can't print Japanese.
    sys.stdout.reconfigure(encoding="utf-8")

    jmdict_path = RAW_DIR / "JMdict_e.gz"
    kanjium_path = RAW_DIR / "accents.txt"
    download(JMDICT_URL, jmdict_path, args.refresh)
    download(KANJIUM_URL, kanjium_path, args.refresh)

    jmdict_bytes = jmdict_path.read_bytes()
    kanjium_bytes = kanjium_path.read_bytes()
    xml_text = gzip.decompress(jmdict_bytes).decode("utf-8")
    accents_text = kanjium_bytes.decode("utf-8")

    header = (
        f"# jmdict_date={jmdict_date(xml_text)}"
        f" jmdict_sha256={sha256(jmdict_bytes)}"
        f" kanjium_sha256={sha256(kanjium_bytes)}"
    )
    output, stats = build(xml_text, accents_text, header)
    output_bytes = output.encode("utf-8")
    OUTPUT_PATH.write_bytes(output_bytes)

    print(f"Wrote {OUTPUT_PATH}")
    print_stats(stats, len(output_bytes))
    return 0


if __name__ == "__main__":
    sys.exit(main())
