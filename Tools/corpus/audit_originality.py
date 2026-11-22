#!/usr/bin/env python3
"""Audits PromptMIDI's output against the whole downloaded corpus: does any generated melody
reproduce a passage of any of the downloaded MIDI files?

    python3 Tools/corpus/audit_originality.py <fingerprints.pickle> <promptmidi-cli> <out_dir> [pieces]

<fingerprints.pickle> is the cache build_originality_index.py writes (every downloaded file's lead-line
fingerprints). The tool composes `pieces` generations with the CLI (a spread of prompts and seeds),
takes every melodic part (melody and counter-melody), and looks each of its runs up - exactly, not
through the Bloom filter - in the fingerprints of all the files. It reports, per piece, the corpus
file sharing the most runs with it and the longest stretch of that part covered by shared runs.

A copied passage of 15 notes or more always shares at least one fingerprint (winnowing guarantees
it); real copying shows up as many shared runs in a row. Isolated shared runs are idioms that many
tunes contain.
"""
import collections
import json
import os
import pickle
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_originality_index import NGRAM, ioi_class, run_hash  # noqa: E402

PROMPTS = [
    "Generate me a fantasy orchestral midi", "Epic orchestral battle music for a boss fight", "Make me a soft piano midi",
    "Lo-fi hip hop beat with a mellow electric piano, rainy night", "Dark fantasy orchestral theme for a villain's castle",
    "Peaceful orchestral village theme with woodwinds and harp", "Late night jazz combo with a tenor sax lead",
    "80s synthwave night drive with arpeggios", "Lively celtic tavern jig with fiddle and whistle",
    "Gentle lullaby for music box and harp", "happy pop song", "country song", "sad piano ballad", "rock song",
    "string quartet in G major", "heroic main theme", "reggae song", "bossa nova", "funky disco track", "christmas song",
]


def melody_runs(track):
    """The same moves as the plug-in (Originality.cpp): the top line on a 16th grid."""
    sky = {}
    for start, length, pitch, *_ in track["notes"]:
        q = (start + 60) // 120
        if q not in sky or pitch > sky[q]:
            sky[q] = pitch
    phrases, cur, prev = [], [], None
    for q in sorted(sky):
        if prev is not None:
            ioi = q - prev
            if ioi <= 0 or ioi > 16:
                if cur:
                    phrases.append(cur)
                cur = []
            else:
                cur.append((max(-12, min(12, sky[q] - sky[prev])) + 12) * 8 + ioi_class(ioi))
        prev = q
    if cur:
        phrases.append(cur)
    runs = []
    for ph in phrases:
        runs.append([run_hash(ph[i:i + NGRAM]) for i in range(len(ph) - NGRAM + 1)])
    return runs


def main():
    cache, cli, out_dir = sys.argv[1], sys.argv[2], sys.argv[3]
    pieces = int(sys.argv[4]) if len(sys.argv) > 4 else 200
    os.makedirs(out_dir, exist_ok=True)

    with open(cache, "rb") as f:
        every = pickle.load(f)

    names = sorted(every)
    owners = collections.defaultdict(list)
    for i, name in enumerate(names):
        for h in every[name]:
            owners[h].append(i)
    print(f"{len(names)} downloaded lead lines, {len(owners)} distinct fingerprints", flush=True)

    worst = []
    for k in range(pieces):
        prompt = PROMPTS[k % len(PROMPTS)]
        seed = 1000 + k
        path = os.path.join(out_dir, f"p{k:04d}.json")
        subprocess.run([cli, prompt, "--seed", str(seed), "--json", path, "-o", os.path.join(out_dir, "x.mid")],
                       check=True, stdout=subprocess.DEVNULL)
        with open(path) as f:
            comp = json.load(f)

        best = (0, 0, None, None)   # (shared runs with one file, longest covered stretch, file, track)
        for t in comp["tracks"]:
            if t.get("drum") or t.get("role") not in ("melody", "counter_melody", "countermelody") or len(t["notes"]) < 12:
                continue
            per_file = collections.defaultdict(list)
            offset = 0
            for runs in melody_runs(t):
                for i, h in enumerate(runs):
                    for owner in owners.get(h, ()):
                        per_file[owner].append(offset + i)
                offset += len(runs) + NGRAM + 1
            for owner, positions in per_file.items():
                positions.sort()
                longest, a = 0, 0
                for b in range(len(positions)):
                    while positions[b] - positions[a] > 24:
                        a += 1
                    if b - a + 1 >= 2:
                        longest = max(longest, positions[b] - positions[a] + NGRAM + 1)
                cand = (len(positions), longest, names[owner], t["name"])
                if cand[:2] > best[:2]:
                    best = cand
        worst.append((best, prompt, seed))
        os.remove(path)

    worst.sort(key=lambda w: (w[0][1], w[0][0]), reverse=True)
    shared = [w[0][0] for w in worst]
    longest = [w[0][1] for w in worst]
    print(f"{pieces} pieces checked against {len(names)} files")
    print(f"  most runs shared with any one file: max {max(shared)}, median {sorted(shared)[len(shared) // 2]}")
    print(f"  longest stretch covered by runs shared with one file: max {max(longest)} notes "
          f"(0 = never two shared runs near each other)")
    for (n, span, name, track), prompt, seed in worst[:5]:
        print(f"  {span:3d} notes / {n} runs  {prompt!r} seed {seed}  {track}  ~ {name}")


if __name__ == "__main__":
    main()
