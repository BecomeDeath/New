#!/usr/bin/env python3
"""Measures how different repeated generations are.

    python3 Tools/variety.py path/to/promptmidi-cli [n]

For each prompt it makes n generations (new seed: what Generate does) and n variations (same
plan, new take: what Variation does), and reports the average pairwise similarity (0 = nothing in
common, 1 = identical) of:
  theme    the main tune's interval sequence and rhythm (first 24 notes of the melody part)
  contour  the tune's up / down / repeat shape (what the ear follows)
  rhythm   the tune's rhythm bar by bar (onset positions in each of its first 8 bars)
  harmony  the chord sequence (roman numerals)
  form     the section layout
  orch     which instrument carries the tune in each section
plus how many distinct keys, tempi and modes the generations used.
"""
import itertools
import json
import os
import subprocess
import sys
import tempfile

PROMPTS = [
    "Generate me a fantasy orchestral midi",
    "Epic orchestral battle music for a boss fight",
    "Make me a soft piano midi",
    "lofi hip hop beat",
    "Sad emotional orchestral piece",
]


def edit_similarity(a, b):
    if not a and not b:
        return 1.0
    n, m = len(a), len(b)
    d = list(range(m + 1))
    for i in range(1, n + 1):
        prev, d[0] = d[0], i
        for j in range(1, m + 1):
            cur = d[j]
            d[j] = min(d[j] + 1, d[j - 1] + 1, prev + (a[i - 1] != b[j - 1]))
            prev = cur
    return 1.0 - d[m] / max(n, m)


def sections(d):
    marks = [m for m in d.get("markers", []) if not m[1].startswith("Loop")]
    if not marks:
        return [("Piece", 0, d["lengthTicks"])]
    return [(name, t, marks[i + 1][0] if i + 1 < len(marks) else d["lengthTicks"]) for i, (t, name) in enumerate(marks)]


def features(d):
    tracks = [t for t in d["tracks"] if not t["drum"]]
    mel = next((t for t in tracks if t["role"] == "melody"), tracks[0] if tracks else None)
    notes = sorted(n for n in mel["notes"]) if mel else []
    line, last = [], None
    for n in notes:
        if n[0] != last:
            line.append(n)
            last = n[0]
    line = line[:25]
    tpb = d["ticksPerBar"]
    theme = [(b[2] - a[2], (b[0] % tpb) // 120) for a, b in zip(line, line[1:])]
    contour = [(b[2] > a[2]) - (b[2] < a[2]) for a, b in zip(line, line[1:])]
    first_bar = (line[0][0] // tpb) if line else 0
    bars = []
    for bar in range(first_bar, first_bar + 8):
        bars.append(frozenset((n[0] - bar * tpb) // 120 for n in notes if bar * tpb <= n[0] < (bar + 1) * tpb))
    harmony = [c["roman"] for c in d["chords"]]
    form = [s[0] for s in sections(d)]
    leads = []
    for name, a, b in sections(d):
        best, count = "", 0
        for t in tracks:
            c = sum(1 for n in t["notes"] if a <= n[0] < b and (len(n) < 5 or n[4] == 1))
            if c > count:
                best, count = t["name"], c
        leads.append(best)
    return {"theme": theme, "contour": contour, "rhythm": bars, "harmony": harmony, "form": form, "orch": leads,
            "key": d["keyRoot"], "tempo": d["tempo"], "mode": d["mode"]}


def run(cli, prompt, seed, variation, tmp):
    path = os.path.join(tmp, "x.json")
    args = [cli, prompt, "--seed", str(seed), "-o", os.path.join(tmp, "x.mid"), "--json", path]
    if variation:
        args += ["--variation", str(variation)]
    subprocess.run(args, check=True, stdout=subprocess.DEVNULL)
    with open(path) as f:
        return features(json.load(f))


def rhythm_similarity(a, b):
    sims = [len(x & y) / len(x | y) if (x | y) else 1.0 for x, y in zip(a, b)]
    return sum(sims) / max(1, len(sims))


def mean_pairwise(items, key):
    pairs = list(itertools.combinations(items, 2))
    sim = rhythm_similarity if key == "rhythm" else edit_similarity
    return sum(sim(a[key], b[key]) for a, b in pairs) / max(1, len(pairs))


def main():
    cli = sys.argv[1]
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 6
    totals = {"generate": [], "variation": []}
    keys = ("theme", "contour", "rhythm", "harmony", "form", "orch")
    print(f"{'prompt':<42} {'mode':<10} {'theme':>6} {'cont':>6} {'rhy':>6} {'harm':>6} {'form':>6} {'orch':>6}  keys tempi modes")
    with tempfile.TemporaryDirectory() as tmp:
        for prompt in PROMPTS:
            gens = [run(cli, prompt, 1000 + 17 * i, 0, tmp) for i in range(n)]
            vars_ = [run(cli, prompt, 1000, i + 1, tmp) for i in range(n)]
            for label, items in (("generate", gens), ("variation", vars_)):
                row = {k: mean_pairwise(items, k) for k in keys}
                totals[label].append(row)
                spread = f"{len({i['key'] for i in items}):>4} {len({i['tempo'] for i in items}):>5} {len({i['mode'] for i in items}):>5}"
                print(f"{prompt[:42]:<42} {label:<10} " + " ".join(f"{row[k]:>6.2f}" for k in keys) + "  " + spread)
    for label, rows in totals.items():
        avg = {k: sum(r[k] for r in rows) / len(rows) for k in keys}
        print(f"{'average':<42} {label:<10} " + " ".join(f"{avg[k]:>6.2f}" for k in keys))


if __name__ == "__main__":
    main()
