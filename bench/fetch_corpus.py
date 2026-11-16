"""Download the benchmark text corpus: seven Natsume Soseki novels from Aozora Bunko.

The novels are public domain. Aozora Bunko distributes them as Shift_JIS text
with ruby (furigana) markup; this script strips the markup and every line
break and space, and writes one long line of UTF-8 text to data/corpus.txt.

Usage:  python bench/fetch_corpus.py [--out data/corpus.txt]

The downloaded zips are cached in data/raw/aozora/, so a second run is offline.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import re
import sys
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# (title, Aozora Bunko card path). Card 000148 is Natsume Soseki.
NOVELS = [
    ("吾輩は猫である", "000148/files/789_ruby_5639.zip"),
    ("こころ", "000148/files/773_ruby_5968.zip"),
    ("三四郎", "000148/files/794_ruby_4237.zip"),
    ("草枕", "000148/files/776_ruby_6020.zip"),
    ("坊っちゃん", "000148/files/752_ruby_2438.zip"),
    ("それから", "000148/files/56143_ruby_50824.zip"),
    ("門", "000148/files/785_ruby_1656.zip"),
]
BASE_URL = "https://www.aozora.gr.jp/cards/"

RUBY = re.compile(r"《[^》]*》")          # 吾輩《わがはい》 -> 吾輩
RUBY_START = re.compile(r"｜")             # ｜ marks where a ruby base starts
NOTE = re.compile(r"［＃[^］]*］")         # editor's notes, e.g. ［＃「一」は中見出し］
WHITESPACE = re.compile(r"[\s　]+")    # includes the ideographic space


def download(card_path: str, cache_dir: Path) -> bytes:
    cached = cache_dir / Path(card_path).name
    if not cached.exists():
        print(f"  downloading {BASE_URL + card_path}")
        with urllib.request.urlopen(BASE_URL + card_path, timeout=60) as response:
            cached.write_bytes(response.read())
    return cached.read_bytes()


def extract_text(zip_bytes: bytes) -> str:
    with zipfile.ZipFile(io.BytesIO(zip_bytes)) as archive:
        name = next(n for n in archive.namelist() if n.endswith(".txt"))
        raw = archive.read(name)
    return raw.decode("cp932")


def clean(text: str) -> str:
    """Keep only the body text: drop the header, the colophon and all markup."""
    lines = text.splitlines()
    # The header (title, author, a legend of markup symbols) ends at the second
    # line of dashes. Not every file has the legend, so fall back to line 2.
    dash_lines = [i for i, line in enumerate(lines) if line.startswith("-----")]
    start = dash_lines[1] + 1 if len(dash_lines) >= 2 else 2
    # The colophon starts at "底本：" (source edition).
    end = next((i for i, line in enumerate(lines) if line.startswith("底本：")), len(lines))
    body = "\n".join(lines[start:end])
    body = RUBY.sub("", body)
    body = RUBY_START.sub("", body)
    body = NOTE.sub("", body)
    return WHITESPACE.sub("", body)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, default=ROOT / "data" / "corpus.txt")
    args = parser.parse_args()

    cache_dir = ROOT / "data" / "raw" / "aozora"
    cache_dir.mkdir(parents=True, exist_ok=True)

    parts = []
    for title, card_path in NOVELS:
        body = clean(extract_text(download(card_path, cache_dir)))
        print(f"  {card_path}: {len(body):,} characters")
        parts.append(body)

    corpus = "".join(parts)
    args.out.write_text(corpus, encoding="utf-8", newline="")
    digest = hashlib.sha256(corpus.encode("utf-8")).hexdigest()
    print(f"wrote {args.out}: {len(corpus):,} characters, sha256 {digest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
