"""Plot the CSV files written by jss_bench.

Usage:  python bench/plot.py [--results bench/results] [--out docs/figures]

Writes four PNG figures (plots 1-4 of docs/PLAN-dictionary.md) and prints a
Markdown summary table for REPORT.md.
"""

from __future__ import annotations

import argparse
import csv
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.ticker import FuncFormatter  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent

# Fixed order and fixed colors: a structure keeps its color in every figure.
# Colors are the first three slots of a CVD-validated categorical palette;
# each structure also has its own marker, so identity never rests on color alone.
STRUCTURES = ["trie", "sorted_array", "hash"]
LABEL = {"trie": "Trie", "sorted_array": "Sorted array", "hash": "Hash map"}
COLOR = {"trie": "#2a78d6", "hash": "#eb6834", "sorted_array": "#1baf7a"}
MARKER = {"trie": "o", "sorted_array": "s", "hash": "^"}

SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK_2 = "#52514e"
GRID = "#e4e3df"

plt.rcParams.update({
    "figure.facecolor": SURFACE,
    "axes.facecolor": SURFACE,
    "savefig.facecolor": SURFACE,
    "axes.edgecolor": INK_2,
    "axes.labelcolor": INK,
    "axes.titlecolor": INK,
    "axes.titlesize": 12,
    "axes.titleweight": "bold",
    "axes.titlelocation": "left",
    "axes.labelsize": 10,
    "axes.spines.top": False,
    "axes.spines.right": False,
    "axes.grid": True,
    "axes.axisbelow": True,
    "grid.color": GRID,
    "grid.linewidth": 0.8,
    "xtick.color": INK_2,
    "ytick.color": INK_2,
    "font.size": 10,
    "legend.frameon": False,
    "lines.linewidth": 2,
    "lines.markersize": 6,
})


def read(path: Path) -> list[dict]:
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def by_structure(rows: list[dict], x: str, y: str) -> dict[str, tuple[list[float], list[float]]]:
    series: dict[str, list[tuple[float, float]]] = defaultdict(list)
    for r in rows:
        series[r["structure"]].append((float(r[x]), float(r[y])))
    return {s: tuple(map(list, zip(*sorted(pts)))) for s, pts in series.items()}  # type: ignore


def human(n: float) -> str:
    for unit, size in (("M", 1e6), ("K", 1e3)):
        if n >= size:
            v = n / size
            return f"{v:.0f}{unit}" if v == int(v) else f"{v:.1f}{unit}"
    return f"{n:g}"


def time_label(ms: float) -> str:
    if ms >= 1000:
        return f"{ms / 1000:.2g} s"
    if ms >= 1:
        return f"{ms:.3g} ms"
    return f"{ms * 1000:.3g} µs"


def plot_lines(ax, series, ylabel_fmt=time_label, legend_loc="upper left"):
    """One line per structure, with a direct label at its right end."""
    for s in STRUCTURES:
        if s not in series:
            continue
        xs, ys = series[s]
        ax.plot(xs, ys, color=COLOR[s], marker=MARKER[s], label=LABEL[s],
                markeredgecolor=SURFACE, markeredgewidth=1.2)
        ax.annotate(f"{LABEL[s]}  {ylabel_fmt(ys[-1])}", (xs[-1], ys[-1]),
                    xytext=(8, 0), textcoords="offset points", va="center",
                    color=INK, fontsize=9)
    ax.legend(loc=legend_loc)


def save(fig, out: Path, name: str):
    path = out / name
    fig.savefig(path, dpi=160, bbox_inches="tight")
    plt.close(fig)
    print(f"wrote {path}")


def plot_text_length(rows, out: Path):
    series = by_structure(rows, "text_length", "ns")
    series = {s: (xs, [y / 1e6 for y in ys]) for s, (xs, ys) in series.items()}
    fig, ax = plt.subplots(figsize=(7.5, 4.5))
    plot_lines(ax, series)
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.xaxis.set_major_formatter(FuncFormatter(lambda v, _: human(v)))
    ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: time_label(v)))
    ax.set_xlabel("Text length (characters, log scale)")
    ax.set_ylabel("Time (log scale)")
    ax.set_title("Common-prefix search at every position of the text")
    ax.margins(x=0.02)
    fig.text(0.0, -0.02, "Full dictionary (499,678 keys). Median of 5 runs. "
             "Text: Natsume Sōseki novels from Aozora Bunko.", color=INK_2, fontsize=8)
    save(fig, out, "text_length.png")


def plot_dict_size(rows, out: Path):
    series = by_structure(rows, "keys", "ns")
    series = {s: (xs, [y / 1e6 for y in ys]) for s, (xs, ys) in series.items()}
    text_length = int(rows[0]["text_length"])
    fig, ax = plt.subplots(figsize=(7.5, 4.5))
    plot_lines(ax, series, legend_loc="center left")
    ax.set_yscale("log")
    ax.xaxis.set_major_formatter(FuncFormatter(lambda v, _: human(v)))
    ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: time_label(v)))
    ax.set_xlabel("Dictionary size (keys)")
    ax.set_ylabel("Time (log scale)")
    ax.set_title(f"Common-prefix search on a {human(text_length)}-character text "
                 "vs. dictionary size")
    fig.text(0.0, -0.02, "Random subsets (10%–100%) of the dictionary's keys, same subset "
             "for every structure. Median of 5 runs.", color=INK_2, fontsize=8)
    save(fig, out, "dict_size.png")


def plot_memory_build(rows, out: Path):
    full = max(int(r["percent"]) for r in rows)
    at_full = {r["structure"]: r for r in rows if int(r["percent"]) == full}
    order = [s for s in STRUCTURES if s in at_full]
    names = [LABEL[s] for s in order]

    # Two panels, two measures: never one chart with two y-scales.
    fig, (left, right) = plt.subplots(1, 2, figsize=(9, 3.2))
    panels = [
        (left, [int(at_full[s]["heap_bytes"]) / 2**20 for s in order], "Heap memory (MB)",
         lambda v: f"{v:.0f} MB"),
        (right, [float(at_full[s]["build_ms"]) for s in order], "Build time (ms)",
         lambda v: f"{v:.0f} ms"),
    ]
    for ax, values, title, fmt in panels:
        bars = ax.barh(names, values, color=[COLOR[s] for s in order], height=0.55,
                       edgecolor=SURFACE, linewidth=2)
        ax.invert_yaxis()
        ax.set_title(title)
        ax.grid(axis="y", visible=False)
        ax.spines["left"].set_visible(False)
        ax.tick_params(axis="y", length=0)
        ax.set_xlim(0, max(values) * 1.25)
        for bar, v in zip(bars, values):
            ax.text(bar.get_width(), bar.get_y() + bar.get_height() / 2, "  " + fmt(v),
                    va="center", color=INK, fontsize=9)
    keys = int(next(iter(at_full.values()))["keys"])
    fig.text(0.0, -0.06, f"Full dictionary ({keys:,} keys). Memory is heap bytes requested "
             "by the structure (allocator overhead not included). Build time is the median of 3.",
             color=INK_2, fontsize=8)
    fig.tight_layout()
    save(fig, out, "memory_build.png")


def plot_predictive(rows, out: Path):
    series = by_structure(rows, "mean_results", "ns_per_query")
    series = {s: (xs, [y / 1e6 for y in ys]) for s, (xs, ys) in series.items()}
    fig, ax = plt.subplots(figsize=(7.5, 4.5))
    plot_lines(ax, series, legend_loc="center left")
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.xaxis.set_major_formatter(FuncFormatter(lambda v, _: human(v)))
    ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: time_label(v)))
    ax.set_xlabel("Keys returned per query (log scale)")
    ax.set_ylabel("Time per query (log scale)")
    ax.set_title("Predictive search: every key that starts with a prefix")
    fig.text(0.0, -0.02, "Prefixes grouped by result count (1–9, 10–99, …), up to 25 per group; "
             "each point is a group's mean. No result limit.", color=INK_2, fontsize=8)
    save(fig, out, "predictive.png")


def summary(text_rows, dict_rows, pred_rows) -> str:
    """Markdown tables for REPORT.md."""
    lines = []
    longest = max(int(r["text_length"]) for r in text_rows)
    t = {r["structure"]: r for r in text_rows if int(r["text_length"]) == longest}
    full = max(int(r["percent"]) for r in dict_rows)
    d = {r["structure"]: r for r in dict_rows if int(r["percent"]) == full}
    lines.append(f"| Structure | Scan {human(longest)} chars | ns/char | Build | Heap | Heap blocks |")
    lines.append("| --- | ---: | ---: | ---: | ---: | ---: |")
    for s in STRUCTURES:
        if s in t and s in d:
            lines.append(
                f"| {LABEL[s]} | {time_label(float(t[s]['ns']) / 1e6)} | {float(t[s]['ns_per_char']):.0f} | "
                f"{float(d[s]['build_ms']):.0f} ms | {int(d[s]['heap_bytes']) / 2**20:.1f} MB | "
                f"{int(d[s]['heap_blocks']):,} |")
    lines.append("")
    buckets = sorted({int(r["bucket_min"]) for r in pred_rows})
    p = {(r["structure"], int(r["bucket_min"])): r for r in pred_rows}
    lines.append("| Predictive: results per query | " + " | ".join(LABEL[s] for s in STRUCTURES) + " |")
    lines.append("| --- | " + " | ".join("---:" for _ in STRUCTURES) + " |")
    for b in buckets:
        mean = float(p[(STRUCTURES[0], b)]["mean_results"])
        cells = [time_label(float(p[(s, b)]["ns_per_query"]) / 1e6) for s in STRUCTURES]
        lines.append(f"| {mean:,.0f} (group {b:,}+) | " + " | ".join(cells) + " |")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--results", type=Path, default=ROOT / "bench" / "results")
    parser.add_argument("--out", type=Path, default=ROOT / "docs" / "figures")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    text_rows = read(args.results / "text_length.csv")
    dict_rows = read(args.results / "dict_size.csv")
    pred_rows = read(args.results / "predictive.csv")

    plot_text_length(text_rows, args.out)
    plot_dict_size(dict_rows, args.out)
    plot_memory_build(dict_rows, args.out)
    plot_predictive(pred_rows, args.out)
    print()
    print(summary(text_rows, dict_rows, pred_rows))
    return 0


if __name__ == "__main__":
    sys.exit(main())
