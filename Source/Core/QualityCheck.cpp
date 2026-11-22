#include "QualityCheck.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace pm
{

namespace
{
    bool isChordalArticulation (Articulation a)
    {
        return a == Articulation::Sustain || a == Articulation::Marcato || a == Articulation::Spiccato
            || a == Articulation::Pizzicato || a == Articulation::Tremolo || a == Articulation::Staccato;
    }

    bool isChordalNote (const Track& t, const Note& n)
    {
        if (t.isDrum)
            return false;

        if (n.articulation == Articulation::Default)
            return t.role == Role::Chords || t.role == Role::Pad;

        return isChordalArticulation (n.articulation);
    }

    bool isMelodicNote (const Track& t, const Note& n)
    {
        if (t.isDrum)
            return false;

        if (n.articulation == Articulation::Default)
            return t.role == Role::Melody;

        return n.articulation == Articulation::Legato || n.articulation == Articulation::Trill;
    }

    std::vector<int> pcsOf (const ChordSlot& s)
    {
        if (! s.pcs.empty())
            return s.pcs;

        Chord c;

        if (parseChordSymbol (s.name, 0, Mode::Major, c))
            return c.pitchClasses();

        return {};
    }

    struct SlotIndex
    {
        explicit SlotIndex (const Composition& c) : comp (c)
        {
            for (auto& s : c.chords)
                pcs.push_back (pcsOf (s));
        }

        int find (int tick) const
        {
            const auto& ch = comp.chords;
            auto it = std::upper_bound (ch.begin(), ch.end(), tick, [] (int t, const ChordSlot& s) { return t < s.start; });
            return it == ch.begin() ? -1 : (int) (it - ch.begin()) - 1;
        }

        bool contains (int slot, int pitch) const
        {
            if (slot < 0)
                return true;

            const int pc = ((pitch % 12) + 12) % 12;

            if (comp.chords[(size_t) slot].bassPc == pc)
                return true;

            const auto& p = pcs[(size_t) slot];
            return std::find (p.begin(), p.end(), pc) != p.end();
        }

        const Composition& comp;
        std::vector<std::vector<int>> pcs;
    };

    struct Sounding
    {
        size_t track;
        size_t note;
    };

    std::vector<Sounding> soundingAt (const Composition& c, int tick)
    {
        std::vector<Sounding> out;

        for (size_t t = 0; t < c.tracks.size(); ++t)
        {
            const auto& tr = c.tracks[t];

            if (tr.isDrum)
                continue;

            for (size_t i = 0; i < tr.notes.size(); ++i)
            {
                const auto& n = tr.notes[i];

                if (n.start > tick)
                    break;

                if (n.start + n.length > tick)
                    out.push_back ({ t, i });
            }
        }

        return out;
    }

    bool harshInterval (int a, int b)
    {
        const int iv = std::abs (a - b) % 12;
        return iv == 1 || iv == 11;
    }

    int beatTicksOf (const Composition& c)
    {
        return c.tsDen == 8 ? kPpq * 3 / 2 : kPpq;
    }

    std::string fmt (const char* format, double a)
    {
        char buf[160];
        std::snprintf (buf, sizeof (buf), format, a);
        return buf;
    }
}

QualityReport checkQuality (const Composition& c)
{
    QualityReport r;

    if (c.tracks.empty() || c.lengthTicks <= 0)
        return r;

    const SlotIndex slots (c);
    const int beat = beatTicksOf (c);

    for (auto& t : c.tracks)
    {
        r.notes += (int) t.notes.size();

        if (t.isDrum)
            continue;

        int lo = 0, hi = 127;
        instrumentRange (t.program, lo, hi);

        for (auto& n : t.notes)
        {
            if (n.pitch < lo || n.pitch > hi)
                ++r.outOfRange;

            if (isChordalNote (t, n) && ! slots.contains (slots.find (n.start + 30), n.pitch))
                ++r.wrongChordNotes;
        }
    }

    int beats = 0, clashes = 0, mud = 0;

    for (int tick = 0; tick < c.lengthTicks; tick += beat)
    {
        const int probe = tick + 30;
        const int slot = slots.find (probe);
        const auto snd = soundingAt (c, probe);

        if (snd.size() < 2)
            continue;

        ++beats;
        bool harsh = false, muddy = false;

        for (size_t i = 0; i < snd.size() && ! (harsh && muddy); ++i)
        {
            const int a = c.tracks[snd[i].track].notes[snd[i].note].pitch;

            for (size_t j = i + 1; j < snd.size(); ++j)
            {
                const int b = c.tracks[snd[j].track].notes[snd[j].note].pitch;

                if (harshInterval (a, b) && ! (slots.contains (slot, a) && slots.contains (slot, b)))
                    harsh = true;

                if (a < 48 && b < 48 && a != b && std::abs (a - b) < 7 && std::abs (a - b) % 12 != 0)
                    muddy = true;
            }
        }

        clashes += harsh ? 1 : 0;
        mud += muddy ? 1 : 0;
    }

    r.strongClashPct = beats > 0 ? 100.0 * clashes / beats : 0.0;
    r.lowMudPct = beats > 0 ? 100.0 * mud / beats : 0.0;

    // Melodic lines, measured per part (doublings, runs and counter-lines are separate lines).
    // A step is a move to the next note of the scale: a second, or the gap between neighbouring
    // degrees of a pentatonic / hexatonic scale (a minor third in a pentatonic tune is a step).
    const auto& scale = modeInfo (c.mode).melodyScale;
    std::vector<bool> inScale (12, false);

    for (int iv : scale)
        inScale[(size_t) (((c.keyRoot + iv) % 12 + 12) % 12)] = true;

    const bool gapped = scale.size() < 7;

    auto isStep = [&] (int a, int b, int iv)
    {
        if (iv >= 1 && iv <= 2)
            return true;

        if (! gapped || iv > 4 || iv < 3)
            return false;

        const int lo = std::min (a, b), hi = std::max (a, b);

        if (! inScale[(size_t) (lo % 12)] || ! inScale[(size_t) (hi % 12)])
            return false;

        for (int p = lo + 1; p < hi; ++p)
            if (inScale[(size_t) (p % 12)])
                return false;

        return true;
    };

    int steps = 0, intervals = 0, widest = 0;
    int lastStart = -1, lastPitch = -1;
    const int barTicks = std::max (1, c.ticksPerBar);

    for (auto& t : c.tracks)
    {
        std::vector<std::pair<int, int>> line;

        for (auto& n : t.notes)
            if (isMelodicNote (t, n) && n.length >= kPpq / 4)
                line.push_back ({ n.start, n.pitch });

        std::sort (line.begin(), line.end(), [] (auto& a, auto& b) { return a.first != b.first ? a.first < b.first : a.second > b.second; });
        line.erase (std::unique (line.begin(), line.end(), [] (auto& a, auto& b) { return a.first == b.first; }), line.end());

        // Range within any 8-bar window of the part.
        for (size_t i = 0, j = 0; i < line.size(); ++i)
        {
            while (line[i].first - line[j].first > 8 * barTicks)
                ++j;

            for (size_t k = j; k < i; ++k)
                widest = std::max (widest, std::abs (line[i].second - line[k].second));
        }

        for (size_t i = 1; i < line.size(); ++i)
        {
            if (line[i].first - line[i - 1].first > barTicks)
                continue; // a new phrase

            const int iv = std::abs (line[i].second - line[i - 1].second);
            ++intervals;
            steps += isStep (line[i - 1].second, line[i].second, iv) ? 1 : 0;
        }

        if (! line.empty() && line.back().first > lastStart)
        {
            lastStart = line.back().first;
            lastPitch = line.back().second;
        }
    }

    const bool haveMelody = intervals > 3;

    if (haveMelody)
    {
        r.melodyStepPct = 100.0 * steps / intervals;
        r.melodyRange = widest;
        r.endsOnTonic = ((lastPitch - c.endKeyRoot) % 12 + 12) % 12 == 0;
    }

    double score = 10.0;
    score -= std::min (3.0, 0.5 * r.outOfRange);
    score -= std::min (3.0, 0.25 * r.wrongChordNotes);
    score -= 0.15 * r.strongClashPct;
    score -= 0.05 * r.lowMudPct;

    if (haveMelody)
    {
        if (r.melodyStepPct < 35.0 || r.melodyStepPct > 88.0)  score -= 0.6;
        if (r.melodyRange < 7)                                 score -= 0.6;
        if (! r.endsOnTonic)                                   score -= 0.4;
    }

    r.score = std::max (0.0, std::min (10.0, score));

    r.notesText.push_back (std::to_string (r.outOfRange) + " notes out of range");
    r.notesText.push_back (std::to_string (r.wrongChordNotes) + " harmony notes outside the chord");
    r.notesText.push_back (fmt ("%.1f%% of beats with a harsh clash", r.strongClashPct));
    r.notesText.push_back (fmt ("%.1f%% of beats with low-register mud", r.lowMudPct));

    if (haveMelody)
        r.notesText.push_back (fmt ("melody: %.0f%% stepwise", r.melodyStepPct) + ", range " + std::to_string (r.melodyRange)
                               + " semitones, " + (r.endsOnTonic ? "ends on the tonic" : "does not end on the tonic"));

    return r;
}

int repairComposition (Composition& c)
{
    int edits = 0;

    // 1. Out-of-range notes move by octaves.
    for (auto& t : c.tracks)
    {
        if (t.isDrum)
            continue;

        int lo = 0, hi = 127;
        instrumentRange (t.program, lo, hi);

        for (auto& n : t.notes)
        {
            const int before = n.pitch;

            while (n.pitch < lo && n.pitch + 12 <= hi) n.pitch += 12;
            while (n.pitch > hi && n.pitch - 12 >= lo) n.pitch -= 12;

            edits += n.pitch != before ? 1 : 0;
        }
    }

    // 2. Harmony notes that aren't in their chord move to the nearest chord tone that doesn't
    //    rub against the melody or double a note the same part is already playing.
    //    (A melody appoggiatura over a correct chord tone is left alone: that's expressive.)
    const SlotIndex slots (c);

    for (size_t ti = 0; ti < c.tracks.size(); ++ti)
    {
        auto& track = c.tracks[ti];

        if (track.isDrum)
            continue;

        int lo = 0, hi = 127;
        instrumentRange (track.program, lo, hi);

        for (size_t ni = 0; ni < track.notes.size(); ++ni)
        {
            auto& n = track.notes[ni];
            const int probe = n.start + 30;
            const int slot = slots.find (probe);

            if (slot < 0 || ! isChordalNote (track, n) || slots.contains (slot, n.pitch))
                continue;

            const auto snd = soundingAt (c, probe);
            int best = -1, bestDist = 99;

            for (int d = -6; d <= 6; ++d)
            {
                const int p = n.pitch + d;

                if (d == 0 || p < lo || p > hi || ! slots.contains (slot, p))
                    continue;

                bool ok = true;

                for (auto& o : snd)
                {
                    const auto& on = c.tracks[o.track].notes[o.note];

                    if (o.track == ti && o.note != ni && on.pitch == p)
                        ok = false;

                    if (isMelodicNote (c.tracks[o.track], on) && harshInterval (on.pitch, p))
                        ok = false;
                }

                if (ok && std::abs (d) < bestDist)
                {
                    bestDist = std::abs (d);
                    best = p;
                }
            }

            if (best >= 0)
            {
                n.pitch = best;
                ++edits;
            }
        }
    }

    return edits;
}

} // namespace pm
