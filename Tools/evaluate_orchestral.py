#!/usr/bin/env python3
"""Generates orchestral pieces with promptmidi-cli and measures them against a professional
(AAA game score) checklist: orchestration, instrument ranges, harmony, voice leading, melody,
form, expression and thematic development.

    python3 Tools/evaluate_orchestral.py path/to/promptmidi-cli out_dir [seed_base]

Writes out_dir/NN-*.mid, out_dir/NN-*.json and prints a report; out_dir/report.json has the numbers.
The scores are heuristics that point at weaknesses - they are not a substitute for listening.

Two scorers are printed:
  v1  the original checklist, kept unchanged for comparison. It predates cues: it caps orchestration
      at 8 and expression at 5.5 (no articulations then), reads form from staggered track entries,
      treats pedal points as wrong notes and takes the whole melody-role track as the tune.
  v2  the same musical criteria made cue-aware (pedals reported separately, the theme measured on
      its own line), plus what the cue engine was asked for: articulations, divisi, runs, separate
      percussion, swell-and-hit transitions, sections and length, and thematic development.
"""
import json
import os
import re
import statistics
import subprocess
import sys

PROMPTS = [
    "Generate me a fantasy orchestral midi",
    "Epic orchestral battle music for a boss fight, 140 bpm",
    "Heroic main theme for an adventure game, triumphant brass and strings",
    "Dark fantasy orchestral theme for a villain's castle",
    "Sad emotional orchestral piece for a character's death scene",
    "Mysterious orchestral exploration music for ancient ruins",
    "Tense orchestral stealth music, sneaking through an enemy base",
    "Orchestral horror music for a haunted mansion",
    "Sci-fi space orchestral theme, vast and wondrous",
    "Peaceful orchestral village theme with woodwinds and harp",
    "Celtic orchestral music for a highland journey",
    "Arabian desert orchestral theme for a sand kingdom",
    "Japanese orchestral theme for a samurai duel",
    "Pirate orchestral adventure on the high seas",
    "Short orchestral victory fanfare",
    "Whimsical magical orchestral music for a wizard school",
    "Fast driving orchestral action chase music",
    "Military orchestral march for an empire's army",
    "Romantic orchestral waltz for a royal ball",
    "Massive dark choir and orchestra for the final boss",
]

# Practical sounding ranges (MIDI notes) for orchestral writing.
RANGES = {
    40: (55, 100, "Violin"), 41: (48, 88, "Viola"), 42: (36, 79, "Cello"), 43: (28, 55, "Contrabass"),
    44: (28, 96, "Tremolo Strings"), 45: (28, 96, "Pizzicato"), 46: (24, 103, "Harp"), 47: (38, 57, "Timpani"),
    48: (28, 100, "Strings"), 49: (28, 100, "Strings"), 52: (40, 81, "Choir"), 53: (40, 81, "Choir"),
    56: (52, 82, "Trumpet"), 57: (40, 72, "Trombone"), 58: (26, 58, "Tuba"), 60: (34, 77, "French Horn"),
    61: (34, 82, "Brass Section"), 68: (58, 91, "Oboe"), 69: (52, 81, "English Horn"), 70: (34, 72, "Bassoon"),
    71: (50, 91, "Clarinet"), 72: (74, 105, "Piccolo"), 73: (60, 96, "Flute"), 8: (60, 108, "Celesta"),
    9: (79, 108, "Glockenspiel"), 14: (60, 77, "Tubular Bells"),
}

SECTION_OF = [
    ("strings", lambda p: 40 <= p <= 51 and p not in (46, 47)),
    ("brass", lambda p: 56 <= p <= 63),
    ("woodwinds", lambda p: 64 <= p <= 79),
    ("choir", lambda p: 52 <= p <= 54),
    ("harp/keys/mallets", lambda p: p == 46 or p <= 15),
    ("timpani", lambda p: p == 47),
]

ORCHESTRAL_PROGRAMS = set(range(40, 80)) | {8, 9, 10, 14}


def slug(text):
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")[:40]


def chord_at(chords, tick):
    for c in chords:
        if c["start"] <= tick < c["end"]:
            return c
    return chords[-1]


def analyse(d):
    tpb, length, key = d["ticksPerBar"], d["lengthTicks"], d["keyRoot"]
    beat = 480 if d["tsDen"] == 4 else 720
    chords, tracks = d["chords"], d["tracks"]
    pitched = [t for t in tracks if not t["drum"]]
    m = {"title": d["title"], "style": d["style"], "summary": d["summary"], "seconds": round(length / 480 * 60 / d["tempo"], 1)}

    # Orchestration
    programs = [t["program"] for t in pitched]
    sections = [name for name, test in SECTION_OF if any(test(p) for p in programs)]
    if any(t["drum"] for t in tracks):
        sections.append("percussion")
    m["sections"] = sections
    m["tracks"] = len(tracks)
    m["non_orchestral"] = sorted({t["name"] for t in pitched if t["program"] not in ORCHESTRAL_PROGRAMS})

    out_of_range, total_notes = 0, 0
    range_detail = {}
    for t in pitched:
        lo, hi, name = RANGES.get(t["program"], (0, 127, ""))
        bad = [n for n in t["notes"] if not lo <= n[2] <= hi]
        out_of_range += len(bad)
        total_notes += len(t["notes"])
        if bad:
            range_detail[t["name"]] = len(bad)
    m["out_of_range_pct"] = round(100 * out_of_range / max(1, total_notes), 1)
    m["out_of_range"] = range_detail

    # Vertical sonority, sampled every 16th note
    clash, mud, samples = 0, 0, 0
    strong_clash, strong_samples = 0, 0
    for grid in range(0, length, 120):
        tick = grid + 30  # just after the grid line: ignore legato overlaps of a few ticks
        chord = chord_at(chords, tick)
        pcs = set(chord["pcs"])
        sounding = [n[2] for t in pitched for n in t["notes"] if n[0] <= tick < n[0] + n[1]]
        if len(sounding) < 2:
            continue
        samples += 1
        harsh = False
        muddy = False
        for i in range(len(sounding)):
            for j in range(i + 1, len(sounding)):
                a, b = sounding[i], sounding[j]
                iv = abs(a - b)
                if iv % 12 in (1, 11) and not (a % 12 in pcs and b % 12 in pcs):
                    harsh = True
                if a < 48 and b < 48 and 0 < iv < 7:
                    muddy = True
        clash += harsh
        mud += muddy
        if grid % beat == 0:
            strong_samples += 1
            strong_clash += harsh
    m["clash_pct"] = round(100 * clash / max(1, samples), 1)
    m["strong_clash_pct"] = round(100 * strong_clash / max(1, strong_samples), 1)
    m["low_mud_pct"] = round(100 * mud / max(1, samples), 1)

    # Melody
    melody_track = next((t for t in pitched if t["role"] == "melody"), None)
    if melody_track and len(melody_track["notes"]) > 3:
        by_start = {}
        for n in melody_track["notes"]:
            if n[0] not in by_start or n[2] > by_start[n[0]][2]:
                by_start[n[0]] = n
        mel = [by_start[k] for k in sorted(by_start)]
        pitches = [n[2] for n in mel]
        ivs = [b - a for a, b in zip(pitches, pitches[1:])]
        m["melody_instrument"] = melody_track["name"]
        m["melody_notes"] = len(mel)
        m["melody_range"] = max(pitches) - min(pitches)
        m["step_pct"] = round(100 * sum(1 for i in ivs if 0 < abs(i) <= 2) / max(1, len(ivs)))
        m["repeat_pct"] = round(100 * sum(1 for i in ivs if i == 0) / max(1, len(ivs)))
        m["leap_pct"] = round(100 * sum(1 for i in ivs if abs(i) >= 5) / max(1, len(ivs)))
        m["largest_leap"] = max((abs(i) for i in ivs), default=0)
        durations = {round(n[1] / 60) for n in mel}
        m["rhythm_values"] = len(durations)
        top = max(pitches)
        m["climax_pos"] = round(mel[pitches.index(top)][0] / length, 2)
        m["ends_on_tonic"] = pitches[-1] % 12 == d.get("endKeyRoot", key)
        strong = [n for n in mel if n[0] % beat < 30]
        m["strong_chord_tone_pct"] = round(100 * sum(1 for n in strong if n[2] % 12 in chord_at(chords, n[0])["pcs"]) / max(1, len(strong)))
        grams = [tuple(ivs[i:i + 4]) for i in range(len(ivs) - 3)]
        m["motif_recurrence_pct"] = round(100 * sum(1 for g in grams if grams.count(g) > 1) / max(1, len(grams)))
        entry = melody_track["entryBar"]
        m["melody_entry_bar"] = entry

        # Outer voices: parallel 5ths / octaves between melody and bass at shared onsets
        bass_track = next((t for t in pitched if t["role"] == "bass"), None)
        if bass_track:
            bass_on = {}
            for n in bass_track["notes"]:
                if n[0] not in bass_on or n[2] < bass_on[n[0]]:
                    bass_on[n[0]] = n[2]
            pairs = [(bass_on[n[0]], n[2]) for n in mel if n[0] in bass_on]
            par = 0
            for (b1, s1), (b2, s2) in zip(pairs, pairs[1:]):
                if b1 != b2 and s1 != s2 and (b2 - b1) * (s2 - s1) > 0:
                    if (s1 - b1) % 12 == (s2 - b2) % 12 and (s1 - b1) % 12 in (0, 7):
                        par += 1
            m["outer_parallels"] = par
            m["outer_pairs"] = len(pairs)
    else:
        m["melody_notes"] = 0

    # Harmony & form
    roman_cycle = d["summary"].split(" · ")[-1]
    m["progression"] = roman_cycle
    names = [c["name"] for c in chords]
    roman_by_section = {c["roman"] for c in chords}
    m["has_b_harmony"] = len(roman_by_section) > len(set(re.split(r" \u2013 ", roman_cycle)))
    m["key_change"] = d.get("endKeyRoot", key) != key
    m["distinct_chords"] = len(set(names))
    m["chord_changes"] = len(names)
    m["entry_bars"] = sorted({t["entryBar"] for t in tracks})
    vels = [n[3] for t in tracks for n in t["notes"]]
    m["velocity_span"] = (min(vels), max(vels)) if vels else (0, 0)
    bars4 = []
    for b in range(0, d["bars"], 4):
        v = [n[3] for t in pitched for n in t["notes"] if b * tpb <= n[0] < (b + 4) * tpb]
        bars4.append(round(statistics.mean(v)) if v else 0)
    m["loudness_by_4_bars"] = bars4
    m["cc_tracks"] = sum(1 for t in pitched if t["cc"] > 0)
    m["melody_doubled"] = any(t["role"] == "countermelody" and t["entryBar"] > 0 and len(t["notes"]) > 0 for t in pitched)
    m["sustained_tracks"] = sum(1 for t in pitched if t["role"] in ("pad", "chords", "countermelody", "melody", "bass", "ostinato"))
    return m


def score(m):
    """0-10 per dimension; 10 = what a AAA game score's MIDI mock-up delivers."""
    s = {}
    core = sum(1 for x in ("strings", "brass", "woodwinds") if x in m["sections"])
    colour = sum(1 for x in ("percussion", "timpani", "choir", "harp/keys/mallets") if x in m["sections"])
    orch = 2.0 * core + 0.7 * colour - 0.6 * len(m["non_orchestral"]) - 0.15 * m["out_of_range_pct"]
    s["orchestration"] = max(0.0, min(8.0, orch))  # no articulations / divisi: capped at 8
    if m["sections"].count("strings") and m.get("melody_doubled"):
        s["orchestration"] = min(8.5, s["orchestration"] + 0.5)

    harm = 8.0 - 0.25 * m["strong_clash_pct"] - 0.05 * m["clash_pct"] - 0.1 * m["low_mud_pct"] - 0.4 * m.get("outer_parallels", 0)
    harm += min(1.5, 0.25 * (m["distinct_chords"] - 3))
    harm -= 0.0 if m["has_b_harmony"] else 1.0     # B section with its own harmony
    harm -= 0.0 if m["key_change"] else 0.5         # final statement lifts to a new key
    s["harmony"] = max(0.0, min(10.0, harm))

    if m["melody_notes"]:
        mel = 8.0
        mel -= 0.05 * max(0, abs(m["step_pct"] - 60) - 15)
        mel -= 1.0 if not (8 <= m["melody_range"] <= 20) else 0
        mel -= 1.0 if m["largest_leap"] > 12 else 0
        mel -= 0.05 * max(0, 70 - m["strong_chord_tone_pct"])
        mel -= 1.0 if not (20 <= m["motif_recurrence_pct"] <= 80) else 0
        mel -= 0.5 if not (0.4 <= m["climax_pos"] <= 0.9) else 0
        mel -= 0.5 if m["rhythm_values"] < 3 else 0
        mel -= 0.5 * max(0, m["repeat_pct"] - 25) / 10
        s["melody"] = max(0.0, min(10.0, mel))
    else:
        s["melody"] = 0.0

    entries = len(m["entry_bars"])
    vel = m["loudness_by_4_bars"]
    arc = (max(vel) - min(vel)) if vel else 0
    form = 2.0 + 1.0 * min(3, entries - 1) + min(2.0, arc / 8)
    form += 1.0 if m["has_b_harmony"] else 0.0
    form += 0.5 if m["key_change"] else 0.0
    s["form"] = max(0.0, min(10.0, form))

    lo, hi = m["velocity_span"]
    expr = 2.0 + min(2.0, (hi - lo) / 30) + min(1.5, m["cc_tracks"] * 0.5)
    s["expression"] = max(0.0, min(10.0, expr))  # no CC1 riding on every sustained part, no articulations

    s["overall"] = round(sum(s.values()) / len(s), 1)
    return {k: round(v, 1) for k, v in s.items()}


#==============================================================================
# v2: cue-aware analysis

ART = ["default", "legato", "sustain", "staccato", "spiccato", "marcato", "pizzicato", "tremolo", "trill"]


def sections_of(d):
    marks = [m for m in d.get("markers", []) if not m[1].startswith("Loop")]
    out = []
    for i, (tick, name) in enumerate(marks):
        end = marks[i + 1][0] if i + 1 < len(marks) else d["lengthTicks"]
        out.append((name, tick, end))
    return out


def legato_line(track, a, b):
    # 40 ticks of tolerance: humanised timing can put a section's first note just before its marker.
    notes = sorted((n for n in track["notes"] if n[4] == 1 and a - 40 <= n[0] < b - 40 and n[1] >= 100), key=lambda n: (n[0], -n[2]))
    line, last = [], None
    for n in notes:
        if n[0] != last:
            line.append(n)
            last = n[0]
    return line


def intervals(line):
    return [b[2] - a[2] for a, b in zip(line, line[1:])]


def contour(ivs):
    """Interval classes with direction: unison, step (2nd), third, fourth/fifth, sixth, seventh/octave,
    wider. Motivic identity survives diatonic sequence (a major 2nd becoming a minor 2nd), the way
    an analyst hears a sequence as the same motif."""
    def cls(i):
        a = abs(i)
        c = 0 if a == 0 else 1 if a <= 2 else 2 if a <= 4 else 3 if a <= 7 else 4 if a <= 9 else 5 if a <= 12 else 6
        return c if i >= 0 else -c
    return [cls(i) for i in ivs]


def contains(seq, pattern):
    k = len(pattern)
    return k > 0 and any(seq[i:i + k] == pattern for i in range(len(seq) - k + 1))


def count_in(seq, pattern):
    k = len(pattern)
    return sum(1 for i in range(len(seq) - k + 1) if seq[i:i + k] == pattern) if k else 0


def analyse_v2(d, prompt):
    m = analyse(d)
    tpb, length = d["ticksPerBar"], d["lengthTicks"]
    beat = 480 if d["tsDen"] == 4 else 720
    chords, tracks = d["chords"], d["tracks"]
    pitched = [t for t in tracks if not t["drum"]]
    secs = sections_of(d)
    m["form_sections"] = [s[0] for s in secs]

    # Harmony with pedal points as part of the sonority (reported separately).
    clash = strong = samples = strong_samples = pedal_beats = 0
    for grid in range(0, length, 120):
        tick = grid + 30
        ch = chord_at(chords, tick)
        pcs = set(ch["pcs"])
        pedal = ch.get("bassPc", -1)
        if pedal >= 0:
            pcs.add(pedal)
        snd = [n[2] for t in pitched for n in t["notes"] if n[0] <= tick < n[0] + n[1]]
        if len(snd) < 2:
            continue
        samples += 1
        harsh = any(abs(a - b) % 12 in (1, 11) and not (a % 12 in pcs and b % 12 in pcs)
                    for i, a in enumerate(snd) for b in snd[i + 1:])
        clash += harsh
        if grid % beat == 0:
            strong_samples += 1
            strong += harsh
            pedal_beats += pedal >= 0
    m["v2_clash_pct"] = round(100 * clash / max(1, samples), 1)
    m["v2_strong_clash_pct"] = round(100 * strong / max(1, strong_samples), 1)
    m["pedal_beats_pct"] = round(100 * pedal_beats / max(1, strong_samples), 1)

    # Orchestration detail
    arts = {ART[n[4]] for t in pitched for n in t["notes"]}
    m["articulations"] = sorted(arts - {"default"})
    divisi = False
    for t in pitched:
        if 40 <= t["program"] <= 51:
            starts = {}
            for n in t["notes"]:
                starts[n[0]] = starts.get(n[0], 0) + 1
            divisi = divisi or sum(1 for v in starts.values() if v >= 2) >= 8
    m["divisi"] = divisi
    runs = 0
    for t in pitched:
        ns = sorted(t["notes"])
        k = 1
        for a, b in zip(ns, ns[1:]):
            if 0 < b[0] - a[0] <= 130 and 0 < abs(b[2] - a[2]) <= 2:
                k += 1
                if k == 6:
                    runs += 1
            else:
                k = 1
    m["runs"] = runs
    m["percussion_tracks"] = sum(1 for t in tracks if t["drum"])
    hits = 0
    for name, start, end in secs[1:]:
        swell = any(n[2] in (49, 51, 52) and start - tpb <= n[0] <= start for t in tracks if t["drum"] for n in t["notes"])
        rolls = sum(1 for t in tracks if t["program"] == 47 for n in t["notes"] if start - beat * 2 <= n[0] < start)
        marc = any(n[4] == 5 and n[0] == start for t in pitched for n in t["notes"])
        hits += (swell or rolls >= 4) and marc
    m["swell_hits"] = hits

    # Expression
    with_cc = sum(1 for t in pitched if t["notes"] and t["cc"] > 0)
    m["cc_fraction"] = round(with_cc / max(1, sum(1 for t in pitched if t["notes"])), 2)
    m["articulation_switching_tracks"] = sum(1 for t in pitched if len({n[4] for n in t["notes"]}) >= 2)
    loud = {}
    for name, a, b in secs:
        v = [n[3] for t in pitched for n in t["notes"] if a <= n[0] < b]
        loud[name] = round(statistics.mean(v)) if v else 0
    m["loudness_by_section"] = loud

    # The theme, on its own line
    mel_track = next((t for t in pitched if t["role"] == "melody"), None)
    theme_sec = next((s for s in secs if s[0] == "Theme"), secs[0] if secs else ("", 0, length))
    theme = legato_line(mel_track, theme_sec[1], theme_sec[2]) if mel_track else []
    if len(theme) > 3:
        p = [n[2] for n in theme]
        iv = intervals(theme)
        m["theme_notes"] = len(theme)
        m["theme_range"] = max(p) - min(p)
        m["theme_step_pct"] = round(100 * sum(1 for i in iv if 0 < abs(i) <= 2) / len(iv))
        m["theme_repeat_pct"] = round(100 * sum(1 for i in iv if i == 0) / len(iv))
        m["theme_largest_leap"] = max(abs(i) for i in iv)
        m["theme_rhythms"] = len({round(n[1] / 60) for n in theme})
        strong_notes = [n for n in theme if n[0] % beat < 30]
        def tone(n):
            c = chord_at(chords, n[0] + 30)
            return n[2] % 12 in set(c["pcs"]) | {c.get("bassPc", -1)}
        m["theme_strong_chord_tone_pct"] = round(100 * sum(1 for n in strong_notes if tone(n)) / max(1, len(strong_notes)))
        grams = [tuple(iv[i:i + 3]) for i in range(len(iv) - 2)]
        m["theme_motif_recurrence_pct"] = round(100 * sum(1 for g in grams if grams.count(g) > 1) / max(1, len(grams)))
        m["theme_ends_stable"] = p[-1] % 12 in set(chord_at(chords, theme[-1][0] + 30)["pcs"])

        # Development: where does the theme's head come back, and how?
        head = contour(iv[:3])
        cell = contour(iv[:2])
        found, dev_cells, imitation = [], 0, False
        for name, a, b in secs:
            if (a, b) == (theme_sec[1], theme_sec[2]):
                continue
            lines = [contour(intervals(legato_line(t, a, b))) for t in pitched]
            if any(contains(l, head) for l in lines):
                found.append(name)
            if name in ("Development", "Build"):
                dev_cells = max(dev_cells, max((count_in(l, cell) for l in lines), default=0))
                imitation = imitation or sum(1 for l in lines if count_in(l, cell) >= 2) >= 2
        m["theme_returns_in"] = found
        m["development_cells"] = dev_cells
        m["imitation"] = imitation
        tutti = next((s for s in secs if s[0] == "Theme (tutti)"), None)
        if tutti:
            first = [c["roman"] for c in chords if theme_sec[1] <= c["start"] < theme_sec[2]]
            again = [c["roman"] for c in chords if tutti[1] <= c["start"] < tutti[2]]
            m["reharmonised"] = first != again
        else:
            m["reharmonised"] = None
        top = max((n for t in pitched for n in t["notes"] if n[4] == 1), key=lambda n: n[2], default=None)
        m["lead_climax_pos"] = round(top[0] / length, 2) if top else 0
    else:
        m["theme_notes"] = 0

    m["requested_short"] = any(w in prompt.lower() for w in ("short", "sketch", "fanfare", "stinger"))
    return m


def score_v2(m):
    """0-10 per dimension; 10 = what a AAA game score's MIDI mock-up delivers."""
    s = {}
    core = sum(1 for x in ("strings", "brass", "woodwinds") if x in m["sections"])
    colour = sum(1 for x in ("percussion", "timpani", "choir", "harp/keys/mallets") if x in m["sections"])
    orch = 2.0 * core + 0.25 * colour
    orch += min(1.0, 0.25 * max(0, len(m["articulations"]) - 2))
    orch += 0.5 if m["divisi"] else 0.0
    orch += 0.5 if m["runs"] >= 2 else 0.0
    orch += 0.5 if m["percussion_tracks"] >= 2 else 0.0
    orch += 0.5 if m["swell_hits"] >= 2 else 0.0
    orch -= 0.6 * len(m["non_orchestral"]) + 0.15 * m["out_of_range_pct"]
    s["orchestration"] = max(0.0, min(10.0, orch))

    harm = 8.0 - 0.25 * m["v2_strong_clash_pct"] - 0.05 * m["v2_clash_pct"] - 0.1 * m["low_mud_pct"] - 0.4 * m.get("outer_parallels", 0)
    harm += min(1.5, 0.25 * (m["distinct_chords"] - 3))
    harm += 0.5 if m.get("reharmonised") else 0.0
    harm -= 0.0 if m["has_b_harmony"] else 1.0
    harm -= 0.0 if m["key_change"] else 0.5
    s["harmony"] = max(0.0, min(10.0, harm))

    if m.get("theme_notes"):
        mel = 9.0
        mel -= 0.05 * max(0, abs(m["theme_step_pct"] - 55) - 20)
        mel -= 1.0 if not (8 <= m["theme_range"] <= 22) else 0
        mel -= 1.0 if m["theme_largest_leap"] > 12 else 0
        mel -= 0.05 * max(0, 75 - m["theme_strong_chord_tone_pct"])
        mel -= 1.0 if not (15 <= m["theme_motif_recurrence_pct"] <= 85) else 0
        mel -= 0.5 if m["theme_rhythms"] < 3 else 0
        mel -= 0.5 * max(0, m["theme_repeat_pct"] - 25) / 10
        mel += 0.5 if m["theme_ends_stable"] else 0
        mel += 0.5 if 0.45 <= m["lead_climax_pos"] <= 0.97 else 0
        s["melody"] = max(0.0, min(10.0, mel))
    else:
        s["melody"] = 0.0

    secs = m["form_sections"]
    form = 0.5 + min(2.0, 0.4 * len(secs))
    long_enough = m["seconds"] >= (25 if m["requested_short"] else 110)
    form += 2.0 if long_enough and m["seconds"] <= 260 else 0.0
    form += 1.0 if "Development" in secs or m["requested_short"] else 0.0
    form += 1.0 if "Second Theme" in secs or m["requested_short"] else 0.0
    form += 1.0 if "Climax" in secs else 0.0
    form += 1.0 if secs and secs[-1] in ("Outro", "Stinger") else 0.0
    loud = m["loudness_by_section"]
    if loud:
        quiet = min(loud.values())
        form += min(1.0, (loud.get("Climax", max(loud.values())) - quiet) / 15)
    form += 0.5 if m["key_change"] else 0.0
    s["form"] = max(0.0, min(10.0, form))

    lo, hi = m["velocity_span"]
    expr = 2.0 + min(2.0, (hi - lo) / 30) + 3.0 * m["cc_fraction"]
    expr += 1.5 if m["articulation_switching_tracks"] >= 3 else 0.0
    expr += 1.5 if loud and loud.get("Climax", 0) >= max(loud.values()) - 3 else 0.0
    s["expression"] = max(0.0, min(10.0, expr))

    if m["requested_short"]:
        pass  # a 30-second fanfare or sketch states its tune; development is not expected (left out of the mean)
    elif m.get("theme_notes"):
        dev = 1.0 + min(4.0, 1.0 * len(m["theme_returns_in"]))
        dev += min(2.0, 0.5 * m["development_cells"])
        dev += 1.5 if m["imitation"] else 0.0
        dev += 1.5 if m.get("reharmonised") or m["requested_short"] else 0.0
        s["development"] = max(0.0, min(10.0, dev))
    else:
        s["development"] = 0.0

    s["overall"] = round(sum(s.values()) / len(s), 1)
    if "development" not in s:
        s["development"] = float("nan")
    return {k: round(v, 1) for k, v in s.items()}


def main():
    cli, out_dir = sys.argv[1], sys.argv[2]
    prompts = PROMPTS
    seed_base = int(sys.argv[3]) if len(sys.argv) > 3 else 100
    os.makedirs(out_dir, exist_ok=True)
    results = []

    for i, prompt in enumerate(prompts, 1):
        base = os.path.join(out_dir, f"{i:02d}-{slug(prompt)}")
        subprocess.run([cli, prompt, "--seed", str(seed_base + i), "-o", base + ".mid", "--json", base + ".json"],
                       check=True, stdout=subprocess.DEVNULL)
        with open(base + ".json") as f:
            data = json.load(f)
        m = analyse_v2(data, prompt)
        results.append({"prompt": prompt, "metrics": m, "scores_v1": score(m), "scores": score_v2(m)})

    with open(os.path.join(out_dir, "report.json"), "w") as f:
        json.dump(results, f, indent=1, default=str)

    dims = ["orchestration", "harmony", "melody", "form", "expression", "development", "overall"]
    print(f"{'#':>2}  {'style':<20} {'len':>5} {'orch':>4} {'harm':>4} {'mel':>4} {'form':>4} {'expr':>4} {'dev':>4} {'v2':>4} {'v1':>4}  prompt")
    for i, r in enumerate(results, 1):
        sc, m = r["scores"], r["metrics"]
        length = f"{int(m['seconds'] // 60)}:{int(m['seconds'] % 60):02d}"
        print(f"{i:>2}  {m['style']:<20} {length:>5} " + " ".join(f"{sc[d]:>4}" for d in dims)
              + f" {r['scores_v1']['overall']:>4}  {r['prompt']}")
    avg = {d: round(statistics.mean(r["scores"][d] for r in results if r["scores"][d] == r["scores"][d]), 1) for d in dims}
    v1 = round(statistics.mean(r["scores_v1"]["overall"] for r in results), 1)
    print("    " + " " * 20 + " " * 6 + " ".join(f"{avg[d]:>4}" for d in dims) + f" {v1:>4}  <- average")


if __name__ == "__main__":
    main()
