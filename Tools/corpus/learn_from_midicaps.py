#!/usr/bin/env python3
"""Learns PromptMIDI's melody, rhythm and harmony statistics from the MidiCaps dataset.

MidiCaps (Melechovsky, Roy & Herremans 2024, CC-BY-SA 4.0) annotates the Lakh MIDI Dataset
(Raffel 2016, CC-BY 4.0) with genre, mood, key, meter and chord labels:
https://huggingface.co/datasets/amaai-lab/MidiCaps

    python3 Tools/corpus/learn_from_midicaps.py <train.json> <midi_root> Source/Core/LearnedData.cpp [files]

Only aggregate statistics are kept - interval transition counts, bar rhythm masks and common
four-chord progressions - never a melody or a piece. They are grouped into style families by the
annotations (cinematic, dark, emotional, bright, energetic, jazz, plus everything) and split by
major / minor key:

  melody   order-2 Markov counts of the lead line's diatonic steps (-7..+7 scale steps), learned
           from the most melodic part of each file (high, mostly monophonic)
  rhythm   the lead line's onset / sounding pattern in each bar, on a 16th-note grid, per meter
  harmony  four-chord progressions (roman numerals relative to the annotated key), from the
           annotated chord sequence with repeats merged and slash basses dropped
"""
import collections
import json
import multiprocessing
import os
import random
import re
import sys

import mido

GROUPS = ["all", "cinematic", "dark", "emotional", "bright", "energetic", "jazz"]

MOOD_GROUPS = {
    "cinematic": {"film", "epic", "adventure", "action", "drama", "dramatic", "trailer", "game", "documentary", "space", "movie"},
    "dark": {"dark", "heavy", "deep"},
    "emotional": {"emotional", "love", "romantic", "ballad", "sad", "slow", "dream", "meditative", "calm", "soft"},
    "bright": {"happy", "christmas", "positive", "uplifting", "fun", "funny", "children", "upbeat", "summer", "inspiring", "motivational"},
    "energetic": {"energetic", "action", "party", "sport"},
}
GENRE_GROUPS = {
    "cinematic": {"soundtrack", "classical", "orchestral"},
    "energetic": {"rock", "dance", "techno", "trance", "house", "metal", "alternative", "instrumentalrock"},
    "jazz": {"jazz"},
}

NOTE_PC = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
MAJOR = [0, 2, 4, 5, 7, 9, 11]
MINOR = [0, 2, 3, 5, 7, 8, 10]
MAX_STEP = 7


def pitch_class(name):
    pc = NOTE_PC[name[0]]
    for ch in name[1:]:
        pc += 1 if ch == "#" else -1 if ch == "b" else 0
    return pc % 12


def groups_of(d):
    out = {"all"}
    moods = set(d.get("mood") or [])
    genres = set(d.get("genre") or [])
    for g, tags in MOOD_GROUPS.items():
        if moods & tags:
            out.add(g)
    for g, tags in GENRE_GROUPS.items():
        if genres & tags:
            out.add(g)
    return out


def roman(chord, tonic, minor):
    """'F#m7' in D major -> 'iii'. Triads only (the engine adds its own colour)."""
    m = re.match(r"^([A-G][#b]?)([^/]*)", chord or "")
    if not m:
        return None
    root = (pitch_class(m.group(1)) - tonic) % 12
    q = m.group(2)
    if q.startswith("m7b5") or q.startswith("dim"):
        quality = "dim"
    elif q.startswith("aug"):
        quality = "aug"
    elif q.startswith("m") and not q.startswith("maj"):
        quality = "min"
    else:
        quality = "maj"
    scale = MINOR if minor else MAJOR
    numerals = ["I", "II", "III", "IV", "V", "VI", "VII"]
    best = None
    for deg, pc in enumerate(scale):
        for acc, shift in (("", 0), ("b", -1), ("#", 1)):
            if (pc + shift) % 12 == root:
                cand = (0 if acc == "" else 1, acc, deg)
                if best is None or cand < best:
                    best = cand
    if best is None:
        return None
    _, acc, deg = best
    name = numerals[deg]
    if quality in ("min", "dim"):
        name = name.lower()
    if quality == "dim":
        name += "o"
    if quality == "aug":
        name += "+"
    return acc + name


def lead_line(path, ts_num, ts_den):
    """The most melodic part: high, mostly one note at a time. Returns [(onset16, dur16, pitch)]."""
    mf = mido.MidiFile(path)
    tpb = mf.ticks_per_beat or 480
    parts = collections.defaultdict(list)
    for ti, track in enumerate(mf.tracks):
        t = 0
        on = {}
        for msg in track:
            t += msg.time
            if msg.type == "note_on" and msg.velocity > 0 and msg.channel != 9:
                on[(msg.channel, msg.note)] = t
            elif (msg.type == "note_off" or (msg.type == "note_on" and msg.velocity == 0)) and msg.channel != 9:
                start = on.pop((msg.channel, msg.note), None)
                if start is not None and t > start:
                    parts[(ti, msg.channel)].append((start, t, msg.note))
    best, best_score = None, -1e9
    for key, notes in parts.items():
        if len(notes) < 48:
            continue
        onsets = collections.Counter(n[0] for n in notes)
        mono = sum(1 for c in onsets.values() if c == 1) / len(onsets)
        mean = sum(n[2] for n in notes) / len(notes)
        if mono < 0.75 or mean < 57:
            continue
        score = mean + 25 * mono + min(10, len(notes) / 40)
        if score > best_score:
            best, best_score = key, score
    if best is None:
        return None
    step = tpb / 4.0  # 16th note
    sky = {}
    for s, e, p in parts[best]:
        q = int(round(s / step))
        if q not in sky or p > sky[q][1]:
            sky[q] = (max(1, int(round((e - s) / step))), p)
    return [(q, d, p) for q, (d, p) in sorted(sky.items())]


def analyse(item):
    path, info = item
    try:
        tonic = pitch_class(info["key"].split()[0])
        minor = info["key"].endswith("minor")
        num, den = (int(x) for x in info["time_signature"].split("/"))
        if (num, den) not in ((4, 4), (3, 4), (6, 8), (2, 4)):
            return None
        bar16 = num * 16 // den
        line = lead_line(path, num, den)
    except Exception:
        return None
    out = {"groups": sorted(groups_of(info)), "minor": minor, "meter": f"{num}/{den}",
           "steps": [], "bars": [], "chords": []}

    # Harmony: roman numerals, repeats merged.
    seq = []
    for c in info.get("all_chords") or []:
        r = roman(c, tonic, minor)
        if r and (not seq or seq[-1] != r):
            seq.append(r)
    out["chords"] = seq

    if not line or len(line) < 32:
        return out

    scale = MINOR if minor else MAJOR
    def diatonic(p):
        rel = (p - tonic) % 12
        octave = (p - tonic) // 12
        deg = max(i for i, pc in enumerate(scale) if pc <= rel) if rel >= scale[0] else 0
        if minor and rel == 11:
            deg = 6  # the raised leading tone is still the seventh degree
        return octave * 7 + deg

    # Melody: diatonic steps within phrases (a gap of more than a beat starts a new phrase).
    phrase = []
    for (q0, d0, p0), (q1, d1, p1) in zip(line, line[1:]):
        if q1 - (q0 + d0) > 4 or q1 - q0 > 16:
            if len(phrase) >= 3:
                out["steps"].append(phrase)
            phrase = []
            continue
        step = diatonic(p1) - diatonic(p0)
        if abs(step) <= MAX_STEP:
            phrase.append(step)
    if len(phrase) >= 3:
        out["steps"].append(phrase)

    # Rhythm: onset and sounding masks for each bar of the lead line.
    by_bar = collections.defaultdict(lambda: [0, 0])
    for q, d, p in line:
        bar, pos = divmod(q, bar16)
        by_bar[bar][0] |= 1 << pos
        for k in range(pos, min(bar16, pos + d)):
            by_bar[bar][1] |= 1 << k
    for bar, (onsets, sounding) in by_bar.items():
        if onsets:
            out["bars"].append((onsets, sounding))
    return out


def main():
    train, midi_root, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
    limit = int(sys.argv[4]) if len(sys.argv) > 4 else 30000
    items = []
    with open(train) as f:
        for line in f:
            d = json.loads(line)
            if not d.get("key") or not d.get("time_signature"):
                continue
            path = os.path.join(midi_root, d["location"])
            if os.path.exists(path):
                items.append((path, d))
    random.seed(7)
    random.shuffle(items)
    # Make sure the smaller families are well represented.
    keep, per_group = [], collections.Counter()
    for it in items:
        g = groups_of(it[1])
        rare = g & {"cinematic", "dark", "emotional", "jazz"}
        if len(keep) < limit and (rare or per_group["other"] < limit // 3):
            keep.append(it)
            per_group["other" if not rare else "rare"] += 1
    print(f"analysing {len(keep)} of {len(items)} files", file=sys.stderr)

    melody = {(g, m): collections.Counter() for g in GROUPS for m in (0, 1)}
    rhythm = {(g, meter): collections.Counter() for g in GROUPS for meter in ("4/4", "3/4", "6/8", "2/4")}
    progs = {(g, m): collections.Counter() for g in GROUPS for m in (0, 1)}
    files = collections.Counter()

    with multiprocessing.Pool(os.cpu_count()) as pool:
        for n, r in enumerate(pool.imap_unordered(analyse, keep, chunksize=32)):
            if n % 2000 == 0:
                print(f"  {n}", file=sys.stderr)
            if not r:
                continue
            m = 1 if r["minor"] else 0
            for g in r["groups"]:
                files[g] += 1
                for phrase in r["steps"]:
                    for a, b, c in zip(phrase, phrase[1:], phrase[2:]):
                        melody[(g, m)][(a, b, c)] += 1
                for bar in r["bars"]:
                    rhythm[(g, r["meter"])][bar] += 1
                seq = r["chords"]
                seen = set()
                for i in range(len(seq) - 3):
                    four = tuple(seq[i:i + 4])
                    if len(set(four)) < 3:
                        continue  # oscillations such as V I V I make poor progressions
                    if four not in seen:  # count each progression once per file
                        seen.add(four)
                        progs[(g, m)][four] += 1

    write_cpp(out_path, melody, rhythm, progs, files, len(keep))


def write_cpp(path, melody, rhythm, progs, files, analysed):
    lines = []
    w = lines.append
    w("// Generated by Tools/corpus/learn_from_midicaps.py - do not edit by hand.")
    w("//")
    w("// Aggregate statistics (interval transition counts, bar rhythm masks, common four-chord")
    w("// progressions) learned from the MidiCaps dataset (Melechovsky, Roy & Herremans 2024,")
    w("// CC-BY-SA 4.0, https://huggingface.co/datasets/amaai-lab/MidiCaps), which annotates the")
    w("// Lakh MIDI Dataset (Colin Raffel 2016, CC-BY 4.0, https://colinraffel.com/projects/lmd/).")
    w("// No melody or piece from the corpus is stored. This file is licensed CC-BY-SA 4.0.")
    w(f"// {analysed} files analysed; files per family: " + ", ".join(f"{g} {files[g]}" for g in GROUPS))
    w("")
    w('#include "LearnedData.h"')
    w("")
    w("namespace pm::learned")
    w("{")
    w("")
    w("// Melody: {family, minor, step(-2), step(-1), next step, count}")
    w("const MelodyCount melodyCounts[] = {")
    count = 0
    for (g, m), counter in melody.items():
        gi = GROUPS.index(g)
        for (a, b, c), n in counter.most_common():
            if n < 3:
                break
            w(f"    {{ {gi}, {m}, {a}, {b}, {c}, {n} }},")
            count += 1
    w("};")
    w(f"const int numMelodyCounts = {count};")
    w("")
    w("// Rhythm: {family, meter (0 = 4/4, 1 = 3/4, 2 = 6/8, 3 = 2/4), onset mask, sounding mask, count}")
    w("const RhythmPattern rhythmPatterns[] = {")
    count = 0
    meters = ["4/4", "3/4", "6/8", "2/4"]
    for (g, meter), counter in rhythm.items():
        gi = GROUPS.index(g)
        for (onsets, sounding), n in counter.most_common(160):
            if n < 4:
                break
            w(f"    {{ {gi}, {meters.index(meter)}, 0x{onsets:04x}, 0x{sounding:04x}, {n} }},")
            count += 1
    w("};")
    w(f"const int numRhythmPatterns = {count};")
    w("")
    w("// Harmony: {family, minor, \"four roman numerals\", count}")
    w("const Progression progressions[] = {")
    count = 0
    for (g, m), counter in progs.items():
        gi = GROUPS.index(g)
        for four, n in counter.most_common(220):
            if n < 3:
                break
            w(f'    {{ {gi}, {m}, "{" ".join(four)}", {n} }},')
            count += 1
    w("};")
    w(f"const int numProgressions = {count};")
    w("")
    w("} // namespace pm::learned")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"wrote {path}", file=sys.stderr)


if __name__ == "__main__":
    main()
