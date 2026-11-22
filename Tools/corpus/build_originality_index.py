#!/usr/bin/env python3
"""Builds PromptMIDI's originality index: fingerprints of the melodies in the reference corpus,
so every generated tune can be checked for copied passages (Source/Core/Originality.h).

    python3 Tools/corpus/build_originality_index.py <train.json> <midi_root> Source/Core/OriginalityData.cpp [cache.pickle]

The corpus is the MidiCaps annotation of the Lakh MIDI Dataset (see learn_from_midicaps.py). For each
file the lead line is taken exactly as the learner takes it, then cut into overlapping runs of
NGRAM melodic moves. A move is the interval to the next note (in semitones, -12..12) and the time
to it (a 16th-note class), so a passage is found whatever key or tempo it is played in.

Runs that occur in many files ("up a scale in eighths") are common property and are dropped (the
Lakh set holds several versions of popular songs, so "many" starts above MAX_FILES files); the
rest - the runs that identify a particular piece - are winnowed (the smallest hash of every
WINDOW consecutive runs is kept, which still catches any shared passage of NGRAM + WINDOW notes)
and stored in a Bloom filter. Only the filter's bits are written out: no melody, note or piece.

Checks (Originality.cpp, audit_originality.py) skip runs with fewer than MIN_CHANGES pitch changes
and runs that repeat one cell of up to 4 moves (an ostinato): they match many pieces a little and
identify none. A part counts as copied when
3 or more of its runs within 24 are found in the index - in tests that catches about 9 in 10 whole
corpus tunes (transposed) and none of PromptMIDI's own melodic parts.
"""
import hashlib
import json
import math
import multiprocessing
import os
import pickle
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from learn_from_midicaps import lead_line  # noqa: E402

NGRAM = 10          # moves per run (11 notes)
WINDOW = 5          # winnowing window
MAX_FILES = 20      # runs found in more downloaded files than this are idioms, not identities
MIN_CHANGES = 4     # checks skip runs with fewer pitch changes (repeated notes carry no melodic identity)
BITS_PER_ENTRY = 14
HASHES = 9

IOI_CLASSES = [1, 2, 3, 4, 6, 8, 12, 16]

FNV_OFFSET = 1469598103934665603
FNV_PRIME = 1099511628211
MASK = (1 << 64) - 1


def ioi_class(sixteenths):
    best = 0
    for i, c in enumerate(IOI_CLASSES):
        if abs(c - sixteenths) < abs(IOI_CLASSES[best] - sixteenths):
            best = i
    return best


def moves(line):
    """[(onset16, dur16, pitch)] -> phrases of move tokens (0..199); a gap over a bar splits phrases."""
    phrases, cur = [], []
    for (q0, _, p0), (q1, _, p1) in zip(line, line[1:]):
        ioi = q1 - q0
        if ioi <= 0 or ioi > 16:
            if cur:
                phrases.append(cur)
            cur = []
            continue
        interval = max(-12, min(12, p1 - p0)) + 12
        cur.append(interval * 8 + ioi_class(ioi))
    if cur:
        phrases.append(cur)
    return phrases


def run_hash(tokens):
    h = FNV_OFFSET
    for t in tokens:
        h = ((h ^ (t + 1)) * FNV_PRIME) & MASK
    # final avalanche (splitmix64)
    h = (h + 0x9E3779B97F4A7C15) & MASK
    h = ((h ^ (h >> 30)) * 0xBF58476D1CE4E5B9) & MASK
    h = ((h ^ (h >> 27)) * 0x94D049BB133111EB) & MASK
    return h ^ (h >> 31)


def run_hashes(phrases):
    out = []
    for ph in phrases:
        hs = [run_hash(ph[i:i + NGRAM]) for i in range(len(ph) - NGRAM + 1)]
        out.append(hs)
    return out


def winnow(hashes):
    picked = set()
    for i in range(max(0, len(hashes) - WINDOW + 1)):
        win = hashes[i:i + WINDOW]
        picked.add(min(win))
    if 0 < len(hashes) < WINDOW:
        picked.add(min(hashes))
    return picked


def analyse(path):
    try:
        line = lead_line(path, 4, 4)
    except Exception:
        return None
    if not line or len(line) < 32:
        return None
    hs = run_hashes(moves(line))
    fps = set()
    for ph in hs:
        fps |= winnow(ph)
    return sorted(fps)


def informative(run):
    """Not mostly repeated notes, and not one short cell over and over (see Originality.cpp)."""
    if sum(1 for t in run if t // 8 != 12) < MIN_CHANGES:
        return False
    for period in range(1, 5):
        if all(run[i] == run[i - period] for i in range(period, len(run))):
            return False
    return True


def bloom_positions(h, m):
    h1 = h & 0xFFFFFFFF
    h2 = (h >> 32) | 1
    return [((h1 + i * h2) & 0xFFFFFFFF) % m for i in range(HASHES)]


def learned_selection(meta, root, limit=30000):
    """The files learn_from_midicaps.py learned from (same filter, seed and balancing)."""
    import collections
    import random
    from learn_from_midicaps import groups_of
    items = []
    with open(meta) as f:
        for line in f:
            d = json.loads(line)
            if not d.get("key") or not d.get("time_signature"):
                continue
            path = os.path.join(root, d["location"])
            if os.path.exists(path):
                items.append((path, d))
    random.seed(7)
    random.shuffle(items)
    keep, per_group = [], collections.Counter()
    for it in items:
        g = groups_of(it[1])
        rare = g & {"cinematic", "dark", "emotional", "jazz"}
        if len(keep) < limit and (rare or per_group["other"] < limit // 3):
            keep.append(it)
            per_group["other" if not rare else "rare"] += 1
    return [os.path.relpath(p, root) for p, _ in keep]


def analyse_rel(args):
    root, rel = args
    return rel, analyse(os.path.join(root, rel))


def extract(root, cache):
    """Fingerprints of every MIDI file under root (cached: {relative path: [hashes]})."""
    if os.path.exists(cache):
        with open(cache, "rb") as f:
            return pickle.load(f)
    rels = []
    for dp, _, fn in os.walk(root):
        for name in fn:
            if name.lower().endswith((".mid", ".midi")):
                rels.append(os.path.relpath(os.path.join(dp, name), root))
    rels.sort()
    out = {}
    with multiprocessing.Pool() as pool:
        for n, (rel, fps) in enumerate(pool.imap_unordered(analyse_rel, [(root, r) for r in rels], chunksize=64)):
            if fps:
                out[rel] = fps
            if n % 10000 == 0:
                print(f"  {n} / {len(rels)}", file=sys.stderr, flush=True)
    with open(cache, "wb") as f:
        pickle.dump(out, f)
    return out


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(1)

    meta, root, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
    cache = sys.argv[4] if len(sys.argv) > 4 else out_path + ".fingerprints.pickle"

    # Every downloaded file decides what is an idiom; the files the statistics were learned
    # from go into the index.
    every = extract(root, cache)
    counts = {}
    for fps in every.values():
        for h in fps:
            counts[h] = counts.get(h, 0) + 1

    learned = [r for r in learned_selection(meta, root) if r in every]
    per_file = [every[r] for r in learned]
    kept = sorted({h for fps in per_file for h in fps if counts.get(h, 0) <= MAX_FILES})

    m = max(64 * 8, int(math.ceil(len(kept) * BITS_PER_ENTRY / 64.0)) * 64)
    bits = bytearray(m // 8)
    for h in kept:
        for pos in bloom_positions(h, m):
            bits[pos >> 3] |= 1 << (pos & 7)

    words = [int.from_bytes(bits[i:i + 8], "little") for i in range(0, len(bits), 8)]
    digest = hashlib.sha256(bits).hexdigest()[:16]

    with open(out_path, "w") as f:
        f.write("// Generated by Tools/corpus/build_originality_index.py - do not edit by hand.\n//\n")
        f.write("// A Bloom filter of melodic fingerprints from the MidiCaps / Lakh MIDI corpus (see LearnedData.cpp for\n")
        f.write("// the attribution): it answers \"does this run of melody occur in the corpus?\" and stores no melody.\n")
        f.write(f"// {len(per_file)} lead lines of the files the statistics were learned from; runs found in more than\n")
        f.write(f"// {MAX_FILES} of the {len(every)} downloaded lead lines count as idioms and are left out. {len(kept)} fingerprints.\n\n")
        f.write('#include "OriginalityData.h"\n\nnamespace pm::originality\n{\n\n')
        f.write(f"const int ngram = {NGRAM};\nconst int window = {WINDOW};\nconst int hashes = {HASHES};\n")
        f.write(f"const int pieces = {len(per_file)};\nconst int fingerprints = {len(kept)};\n")
        f.write(f"const unsigned long long bitCount = {m}ull;\n")
        f.write(f'const char* const digest = "{digest}";\n\n')
        f.write(f"const unsigned long long bits[{len(words)}] = {{\n")
        for i in range(0, len(words), 6):
            f.write("    " + ", ".join(f"0x{w:016x}ull" for w in words[i:i + 6]) + ",\n")
        f.write("};\n\n} // namespace pm::originality\n")

    print(f"{len(every)} lead lines downloaded, {len(per_file)} learned from, {len(counts)} fingerprints, "
          f"{len(kept)} distinctive in the index, filter {m} bits ({m // 8 / 1e6:.2f} MB), wrote {out_path}")


if __name__ == "__main__":
    main()
