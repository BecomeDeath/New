#include "Composer.h"
#include "ComposerDetail.h"
#include "Learned.h"
#include "CueEngine.h"
#include "QualityCheck.h"
#include "Rng.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <functional>
#include <map>

namespace pm
{

namespace detail
{

Meter makeMeter (int num, int den)
{
    Meter m;
    m.num = num;
    m.den = den;

    const int unit = kPpq * 4 / den;
    m.ticksPerBar = num * unit;
    m.compound = (den == 8 && num % 3 == 0);

    if (m.compound)
    {
        m.beatsPerBar = num / 3;
        m.beatTicks = unit * 3;
        m.subdivTicks = unit;
    }
    else
    {
        m.beatsPerBar = num;
        m.beatTicks = unit;
        m.subdivTicks = unit / 2;
    }

    m.stepsPerBar = m.ticksPerBar / kStep;
    return m;
}

/** Instruments whose notes decay by themselves, so they can be allowed to ring. */
bool isRinging (int program)
{
    return program <= 15 || program == 24 || program == 25 || program == 45 || program == 46
        || (program >= 104 && program <= 108) || program == 112 || program == 114;
}

bool isPianoLike (int program) { return program <= 7; }
bool isGuitarLike (int program) { return program >= 24 && program <= 31; }

//==============================================================================
char formLetter (int index, int count, int layout)
{
    if (count <= 2)
        return 'A';

    if (count == 3)
    {
        static const char three[][3] = { { 'A', 'B', 'A' }, { 'A', 'A', 'B' }, { 'A', 'B', 'B' } };
        return three[layout % 3][index];
    }

    // A A B A, A B A B, A A B B, A B A A, A B B A: chosen per piece.
    static const char four[][4] = { { 'A', 'A', 'B', 'A' }, { 'A', 'B', 'A', 'B' }, { 'A', 'A', 'B', 'B' },
                                    { 'A', 'B', 'A', 'A' }, { 'A', 'B', 'B', 'A' } };
    const char* form = four[layout % 5];
    return index == count - 1 && count > 4 ? 'A' : form[index % 4];
}

/** Lays out the form: an intro until the first melody enters, then 4-bar phrases in one of
    several song shapes (A A B A, A B A B, A A B B...), with an optional key lift for the final
    statement of the theme.
*/
void planSections (Context& ctx, Rng& rng)
{
    static const double layoutWeights[] = { 0.4, 0.2, 0.15, 0.15, 0.1 };
    double r = rng.uniform();
    int layout = 0;

    for (; layout < 4 && r > layoutWeights[layout]; ++layout)
        r -= layoutWeights[layout];

    const auto& s = ctx.spec;
    int entry = ctx.bars;

    for (auto& t : s.tracks)
        if (t.role == Role::Melody)
            entry = std::min (entry, t.entryBar);

    if (entry >= ctx.bars)
        entry = 0;

    const int barsLeft = ctx.bars - entry;
    const int phraseBars = barsLeft >= 8 ? 4 : (barsLeft >= 4 ? 2 : 1);

    if (entry > 0)
        ctx.sections.push_back ({ 0, entry, 'I', s.keyRoot });

    const int count = (barsLeft + phraseBars - 1) / phraseBars;

    for (int i = 0; i < count; ++i)
    {
        const int start = entry + i * phraseBars;
        ctx.sections.push_back ({ start, std::min (phraseBars, ctx.bars - start), formLetter (i, count, layout), s.keyRoot });
    }

    int statementsOfA = 0;

    for (auto& sec : ctx.sections)
        statementsOfA += sec.type == 'A' ? 1 : 0;

    if (s.finalLift > 0 && statementsOfA >= 2 && ctx.bars >= 12 && ctx.sections.back().type == 'A')
        ctx.sections.back().keyRoot = wrapPc (s.keyRoot + s.finalLift);
}

void buildChords (Context& ctx, Rng& rng)
{
    const auto& s = ctx.spec;
    const int slot = std::max (kStep, (int) std::lround (ctx.meter.ticksPerBar / s.chordsPerBar));

    // Extensions are decided once per progression position so repeats stay consistent.
    struct Colour { bool seventh = false, ninth = false; };

    auto decideColours = [&] (size_t n)
    {
        std::vector<Colour> c (n);

        for (auto& col : c)
        {
            col.seventh = s.complexity > 0.4 && rng.chance ((s.complexity - 0.3) * 1.25);
            col.ninth = s.complexity > 0.7 && rng.chance ((s.complexity - 0.6) * 1.5);
        }

        return c;
    };

    const auto& progA = s.progression;
    const auto& progB = s.progressionB.empty() ? s.progression : s.progressionB;
    const auto coloursA = decideColours (progA.size());
    const auto coloursB = decideColours (progB.size());

    auto makeChord = [&] (const std::string& symbol, const Colour& colour, int key)
    {
        Chord c;

        if (! parseChordSymbol (symbol, key, s.mode, c))
            c = diatonicChord (0, key, s.mode, false);

        if (c.intervals.size() == 3)
        {
            if (colour.seventh) addDiatonicSeventh (c, key, s.mode);
            if (colour.ninth)   addDiatonicNinth (c, key, s.mode);
        }

        return c;
    };

    // Progressions that fit inside a phrase restart with every section (so B can bring
    // new harmony); longer ones (a 12-bar blues, slow ambient changes) simply cycle.
    const int slotsPerPhrase = ctx.sections.empty() ? 0 : (ctx.sections.back().bars * ctx.meter.ticksPerBar) / slot;
    const bool perSection = slotsPerPhrase > 0 && (int) progA.size() <= slotsPerPhrase && slotsPerPhrase % (int) progA.size() == 0
                         && (int) progB.size() <= slotsPerPhrase;

    int globalIndex = 0;

    for (auto& sec : ctx.sections)
    {
        const int secStart = sec.startBar * ctx.meter.ticksPerBar;
        const int secEnd = std::min (ctx.totalTicks, (sec.startBar + sec.bars) * ctx.meter.ticksPerBar);
        const bool useB = perSection && sec.type == 'B';
        const auto& prog = useB ? progB : progA;
        const auto& colours = useB ? coloursB : coloursA;
        int local = 0;

        for (int start = secStart; start < secEnd; start += slot)
        {
            const size_t idx = (size_t) (perSection ? local : globalIndex) % prog.size();
            ChordEvent e;
            e.start = start;
            e.end = std::min (secEnd, start + slot);
            e.keyRoot = sec.keyRoot;
            e.chord = makeChord (prog[idx], colours[idx], sec.keyRoot);
            e.roman = prog[idx];
            ctx.chords.push_back (e);
            ++local;
            ++globalIndex;
        }
    }

    if (ctx.hasFinalBar())
    {
        const int fb = ctx.finalBarStart();
        const int finalKey = ctx.sections.back().keyRoot;
        ctx.chords.erase (std::remove_if (ctx.chords.begin(), ctx.chords.end(),
                                          [fb] (const ChordEvent& e) { return e.start >= fb; }),
                          ctx.chords.end());

        if (! ctx.chords.empty() && ctx.chords.back().end > fb)
            ctx.chords.back().end = fb;

        ChordEvent fin;
        fin.start = fb;
        fin.end = ctx.totalTicks;
        fin.keyRoot = finalKey;
        fin.chord = diatonicChord (0, finalKey, s.mode, s.complexity > 0.7);
        fin.roman = fin.chord.symbol;
        ctx.chords.push_back (fin);
    }

    for (auto& e : ctx.chords)
        e.pcs = e.chord.pitchClasses();
}

//==============================================================================
// Voicing helpers

/** Chord tones in order of importance: root, third, seventh, extensions, fifth. */
std::vector<int> chordToneOrder (const Chord& c)
{
    std::vector<int> order;

    auto add = [&] (int iv)
    {
        if (! c.hasInterval (iv))
            return;

        const int pc = wrapPc (c.rootPc + iv);

        if (std::find (order.begin(), order.end(), pc) == order.end())
            order.push_back (pc);
    };

    for (int iv : { 0, 3, 4, 2, 5, 10, 11, 9, 14, 7, 6, 8 })
        add (iv);

    for (int iv : c.intervals)
        add (iv);

    return order;
}

std::vector<int> wantedPcs (const Chord& c, int voices, bool dropRoot)
{
    auto order = chordToneOrder (c);

    if (dropRoot && order.size() >= 4)
        order.erase (order.begin());

    std::vector<int> wanted (order.begin(), order.begin() + std::min ((size_t) voices, order.size()));

    // Double the root, then the fifth, when there are fewer tones than voices.
    const int doubles[] = { wrapPc (c.rootPc), wrapPc (c.rootPc + 7), wrapPc (c.rootPc + 4) };

    for (int k = 0; (int) wanted.size() < voices && k < 6; ++k)
    {
        const int pc = doubles[k % 3];

        if (c.containsPc (pc) || k % 3 == 0)
            wanted.push_back (pc);
    }

    return wanted;
}

std::vector<int> voiceChord (const std::vector<int>& wanted, int rootPc, int low, int high,
                             const std::vector<int>& prev, int maxSpread, bool rootInBass)
{
    std::vector<std::vector<int>> cands;

    for (int pc : wanted)
    {
        std::vector<int> c;

        for (int p = low; p <= high; ++p)
            if (wrapPc (p) == pc)
                c.push_back (p);

        if (c.empty())
            for (int p = std::max (0, low - 12); p <= std::min (127, high + 12); ++p)
                if (wrapPc (p) == pc)
                    c.push_back (p);

        cands.push_back (c);
    }

    const double mid = (low + high) * 0.5;
    std::vector<int> best, cur (wanted.size());
    double bestScore = 1e18;

    std::function<void (size_t)> rec = [&] (size_t i)
    {
        if (i == wanted.size())
        {
            auto s = cur;
            std::sort (s.begin(), s.end());

            for (size_t j = 1; j < s.size(); ++j)
                if (s[j] == s[j - 1])
                    return;

            double sc = 0.0;
            const int spread = s.back() - s.front();

            if (spread > maxSpread)
                sc += (spread - maxSpread) * 3.0;

            double mean = 0.0;

            for (int p : s)
                mean += p;

            mean /= (double) s.size();
            sc += std::abs (mean - mid) * (prev.empty() ? 0.6 : 0.15);

            if (! prev.empty())
            {
                for (int p : s)
                {
                    int d = 1000;

                    for (int q : prev)
                        d = std::min (d, std::abs (p - q));

                    sc += d;
                }

                sc += std::abs (s.back() - prev.back()) * 0.5;
            }

            for (size_t j = 1; j < s.size(); ++j)
            {
                const int gap = s[j] - s[j - 1];

                if (s[j - 1] < 52 && gap < 5)  sc += 4.0;  // muddy low intervals
                if (gap == 1)                  sc += 2.5;  // semitone clusters
            }

            if (rootInBass && wrapPc (s.front()) != wrapPc (rootPc))
                sc += 6.0;

            if (sc < bestScore)
            {
                bestScore = sc;
                best = s;
            }

            return;
        }

        for (int p : cands[i])
        {
            cur[i] = p;
            rec (i + 1);
        }
    };

    rec (0);

    if (best.empty())
    {
        // The register was too narrow for the doublings: drop them and try again...
        std::vector<int> unique;

        for (int pc : wanted)
            if (std::find (unique.begin(), unique.end(), pc) == unique.end())
                unique.push_back (pc);

        if (unique.size() < wanted.size())
            return voiceChord (unique, rootPc, low, high, prev, maxSpread, rootInBass);

        // ...and if even that fails, stack the chord's own notes upwards from the bottom of the register.
        int next = low;

        for (int pc : unique)
        {
            int p = next;

            while (wrapPc (p) != wrapPc (pc))
                ++p;

            best.push_back (p);
            next = p + 1;
        }

        std::sort (best.begin(), best.end());
    }

    return best;
}

/** Lowest-ish note of a pitch class in the register, staying close to the previous one. */
int bassPitchFor (int pc, int low, int high, int prev)
{
    int best = -1;
    double bestScore = 1e9;

    for (int p = low; p <= high; ++p)
    {
        if (wrapPc (p) != wrapPc (pc))
            continue;

        const double sc = (prev >= 0 ? std::abs (p - prev) : 0) + (p - low) * 0.35;

        if (sc < bestScore)
        {
            bestScore = sc;
            best = p;
        }
    }

    return best >= 0 ? best : low + wrapPc (pc - low);
}

int fifthAbove (int pitch, int high) { return pitch + 7 <= high ? pitch + 7 : pitch - 5; }

//==============================================================================
// Melody


struct Cell
{
    std::vector<int> units; // negative = rest
    double w[3];            // lyrical, rhythmic, sparse
};

const std::vector<Cell>& simpleCells()
{
    // Units are 16th notes; a beat is 4 units.
    static const std::vector<Cell> cells = {
        { { 4 },          { 3.0, 1.5, 2.0 } },
        { { 2, 2 },       { 2.0, 3.0, 0.5 } },
        { { 3, 1 },       { 1.5, 1.5, 0.3 } },
        { { 1, 1, 2 },    { 0.4, 2.0, 0.0 } },
        { { 2, 1, 1 },    { 0.4, 2.0, 0.0 } },
        { { 1, 1, 1, 1 }, { 0.2, 1.5, 0.0 } },
        { { -2, 2 },      { 0.3, 1.2, 0.8 } },
        { { 8 },          { 2.5, 0.5, 3.0 } },
        { { 6, 2 },       { 2.5, 1.2, 1.5 } },
        { { 2, 4, 2 },    { 1.0, 2.0, 0.3 } },
        { { 4, -4 },      { 0.3, 0.3, 1.5 } },
        { { 12 },         { 0.8, 0.0, 1.5 } },
        { { 16 },         { 0.8, 0.0, 2.0 } },
    };

    return cells;
}

const std::vector<Cell>& compoundCells()
{
    // Units are 8th notes; a beat (dotted quarter) is 3 units.
    static const std::vector<Cell> cells = {
        { { 3 },         { 3.0, 1.5, 2.0 } },
        { { 2, 1 },      { 2.5, 2.5, 1.0 } },
        { { 1, 1, 1 },   { 1.0, 3.0, 0.0 } },
        { { 1, 2 },      { 1.0, 1.5, 0.3 } },
        { { -1, 1, 1 },  { 0.3, 1.2, 0.5 } },
        { { 6 },         { 2.0, 0.3, 3.0 } },
        { { 4, 2 },      { 1.5, 0.5, 1.0 } },
        { { 3, -3 },     { 0.3, 0.2, 1.5 } },
    };

    return cells;
}

std::vector<RNote> fillRhythm (const Meter& m, int from, int to, double density, MelStyle style, Rng& rng, bool allowLeadingRest)
{
    const auto& cells = m.compound ? compoundCells() : simpleCells();
    const int unit = m.compound ? m.subdivTicks : kStep;
    const int beatUnits = m.beatTicks / unit;
    const double target = std::pow (2.0, 1.0 - 2.0 * density); // preferred note length in beats

    std::vector<RNote> out;
    int pos = from;

    while (pos < to)
    {
        const int remainingUnits = (to - pos) / unit;

        if (remainingUnits < beatUnits)
        {
            out.push_back ({ pos, to - pos, false });
            break;
        }

        std::vector<double> weights;

        for (auto& c : cells)
        {
            int total = 0, count = 0;

            for (int u : c.units)
            {
                total += std::abs (u);
                count += u > 0 ? 1 : 0;
            }

            double w = c.w[(int) style];

            if (total > remainingUnits || total % beatUnits != 0 || count == 0)
                w = 0.0;

            if (c.units[0] < 0 && pos == from && ! allowLeadingRest)
                w = 0.0;

            const int beatIndex = (pos % m.ticksPerBar) / m.beatTicks;
            const int cellBeats = total / beatUnits;

            if (m.beatsPerBar == 4 && cellBeats == 2 && beatIndex % 2 == 1)
                w *= 0.3; // a half note across the middle of the bar is unusual

            if (w > 0.0)
            {
                const double avg = (double) total / count / beatUnits;
                w *= std::exp (-std::abs (std::log2 (avg) - std::log2 (target)) * 1.1);
            }

            weights.push_back (w);
        }

        const int idx = rng.weighted (weights);

        if (weights[(size_t) idx] <= 0.0)
        {
            out.push_back ({ pos, m.beatTicks, false });
            pos += m.beatTicks;
            continue;
        }

        for (int u : cells[(size_t) idx].units)
        {
            const int d = std::abs (u) * unit;
            out.push_back ({ pos, d, u < 0 });
            pos += d;
        }
    }

    return out;
}

std::vector<RNote> cadenceRhythm (const Meter& m, bool finalNote, bool pickup, double density, MelStyle style, Rng& rng)
{
    std::vector<RNote> out;
    int holdStart = 0;

    if (! finalNote && m.beatsPerBar >= 3 && rng.chance (0.35 + 0.3 * density))
    {
        auto lead = fillRhythm (m, 0, m.beatTicks, std::max (density, 0.5), style, rng, false);
        out.insert (out.end(), lead.begin(), lead.end());
        holdStart = m.beatTicks;
    }

    int holdEnd = m.ticksPerBar;
    const bool doPickup = pickup && ! finalNote && holdEnd - m.beatTicks > holdStart;

    if (doPickup)
        holdEnd -= m.beatTicks;

    out.push_back ({ holdStart, holdEnd - holdStart, false });

    if (doPickup)
    {
        const int sub = m.subdivTicks;

        if (m.compound)
        {
            out.push_back ({ holdEnd, sub, true });
            out.push_back ({ holdEnd + sub, sub, false });
            out.push_back ({ holdEnd + 2 * sub, sub, false });
        }
        else if (rng.chance (0.5))
        {
            out.push_back ({ holdEnd, sub, true });
            out.push_back ({ holdEnd + sub, sub, false });
        }
        else
        {
            out.push_back ({ holdEnd, sub, true });
            out.push_back ({ holdEnd + sub, sub / 2, false });
            out.push_back ({ holdEnd + sub + sub / 2, sub / 2, false });
        }
    }

    return out;
}

bool isStrongPosition (const Meter& m, int offsetInBar, int dur)
{
    if (dur >= m.beatTicks * 2)
        return true;

    if (offsetInBar % m.beatTicks != 0)
        return false;

    const int beat = offsetInBar / m.beatTicks;
    return beat == 0 || (m.beatsPerBar == 4 && beat == 2);
}

bool indexIsChordTone (const ScaleMap& sm, int idx, const ChordEvent& ch)
{
    return ch.chord.containsPc (sm.pitch (idx));
}

/** Picks the next melody note as a scale index.
    @param leapBias  0 for normal voice leading, or the size (in scale steps, signed) of a
                     signature leap the theme should make here (e.g. +4 = a rising fifth).
*/
int chooseMelodyIndex (int prev, int prevDelta, int target, int lo, int hi, const ChordEvent& ch,
                       bool strong, bool allowRepeat, Rng& rng, const ScaleMap& sm, int leapBias, bool onBeat)
{
    int best = clampv (prev, lo, hi);
    double bestScore = 1e9;

    for (int cand = std::max (lo, prev - 7); cand <= std::min (hi, prev + 7); ++cand)
    {
        const int d = cand - prev;
        const int ad = std::abs (d);
        double s = 0.0;

        if (leapBias != 0)
        {
            s += 0.8 * std::abs (d - leapBias) - 0.5;
        }
        else
        {
            static const double stepCost[] = { 1.6, 0.0, 0.6, 1.2, 1.7, 2.3, 3.0, 3.6 };
            s += (ad == 0 && allowRepeat) ? 0.6 : stepCost[std::min (ad, 7)];

            // After a leap, prefer to turn back by step (gap fill).
            if (std::abs (prevDelta) >= 3 && d != 0 && ((d > 0) == (prevDelta > 0)))
                s += 1.5;
        }

        s += 0.35 * std::abs (cand - target);

        // Dissonance belongs on off-beats (passing and neighbour notes); on the beat it clashes
        // with the sustained harmony underneath.
        const bool ct = indexIsChordTone (sm, cand, ch);
        s += strong ? (ct ? -1.5 : 2.5) : (onBeat ? (ct ? -0.8 : 1.4) : (ct ? -0.3 : 0.2));

        if (std::abs (sm.pitch (cand) - sm.pitch (prev)) == 6)
            s += 2.0; // avoid tritone leaps

        s += rng.uniform (0.0, 0.9);

        if (s < bestScore)
        {
            bestScore = s;
            best = cand;
        }
    }

    return best;
}


int cadenceIndex (const ScaleMap& sm, int prev, int lo, int hi, const ChordEvent& ch, CadencePref pref, int tonicPc)
{
    int best = kNoIndex;
    double bestScore = 1e9;

    for (int idx = lo; idx <= hi; ++idx)
    {
        const int pc = wrapPc (sm.pitch (idx));
        double s = std::abs (idx - prev);

        if (pref == CadencePref::Tonic)
        {
            if (pc != wrapPc (tonicPc))
                continue;
        }
        else
        {
            if (! ch.chord.containsPc (pc))
                continue;

            const bool isRoot = pc == wrapPc (ch.chord.rootPc);

            if (pref == CadencePref::Root)
                s += isRoot ? -1.5 : 0.0;
            else
                s += isRoot ? 1.0 : -0.5;
        }

        if (s < bestScore)
        {
            bestScore = s;
            best = idx;
        }
    }

    return best == kNoIndex ? clampv (prev, lo, hi) : best;
}

int snapToChordTone (const ScaleMap& sm, int idx, int lo, int hi, const ChordEvent& ch)
{
    if (indexIsChordTone (sm, idx, ch))
        return idx;

    for (int d : { 1, -1, 2, -2 })
        if (idx + d >= lo && idx + d <= hi && indexIsChordTone (sm, idx + d, ch))
            return idx + d;

    return idx;
}

Notes generateMelody (Context& ctx, const TrackSpec& t, Rng& rng, bool useTheme)
{
    const Meter& m = ctx.meter;
    const ScaleMap sm = ctx.melodyScale();
    const int n = sm.size();

    int lo = sm.indexAtOrAbove (t.lowNote);
    int hi = sm.indexAtOrBelow (t.highNote);

    if (hi - lo < n)
        hi = lo + n;

    const int center = (lo + hi) / 2;
    const int span = hi - lo;

    MelStyle style = MelStyle::Lyrical;

    if (t.pattern == "rhythmic")                                style = MelStyle::Rhythmic;
    else if (t.pattern == "sparse")                             style = MelStyle::Sparse;
    else if (t.pattern == "auto" && t.density > 0.68)           style = MelStyle::Rhythmic;
    else if (t.pattern == "auto" && t.density < 0.28)           style = MelStyle::Sparse;

    const double legato = style == MelStyle::Rhythmic ? 0.82 : (style == MelStyle::Sparse ? 1.0 : 0.95);

    // Phrases follow the piece's form (shared with the harmony), from this part's entry on.
    struct Phrase { int startBar, length; char type; bool final; int keyRoot; };
    std::vector<Phrase> phrases;

    for (auto& sec : ctx.sections)
    {
        const int from = std::max (sec.startBar, t.entryBar);
        const int to = sec.startBar + sec.bars;

        if (from >= to)
            continue;

        phrases.push_back ({ from, to - from, sec.type == 'I' ? 'A' : sec.type, false, sec.keyRoot });
    }

    if (phrases.empty())
        return {};

    phrases.back().final = true;

    int phraseBars = 1;

    for (auto& ph : phrases)
        phraseBars = std::max (phraseBars, ph.length);

    struct Plan
    {
        std::vector<std::vector<RNote>> rhythms;
        std::vector<std::vector<int>> indices;
        std::vector<std::vector<bool>> fixed;
        bool used = false;
    };

    std::map<char, Plan> plans;

    // A theme (usually written by Claude) becomes the fixed opening of every 'A' phrase.
    if (useTheme && ! ctx.spec.theme.empty())
    {
        Plan& plan = plans['A'];

        double sum = 0.0;
        int count = 0;

        for (auto& tn : ctx.spec.theme)
            if (! tn.rest)
            {
                sum += tn.degree;
                ++count;
            }

        const double mean = count > 0 ? sum / count : 0.0;
        const int shift = n * (int) std::lround ((center - mean) / n);

        int tick = 0;
        const int phraseTicks = phraseBars * m.ticksPerBar;

        for (int b = 0; b < phraseBars; ++b)
        {
            plan.rhythms.emplace_back();
            plan.indices.emplace_back();
            plan.fixed.emplace_back();
        }

        for (auto& tn : ctx.spec.theme)
        {
            if (tick >= phraseTicks)
                break;

            const int dur = std::max (kStep / 2, (int) std::lround (tn.beats * kPpq));
            const int bar = tick / m.ticksPerBar;
            const int off = tick - bar * m.ticksPerBar;
            const int clipped = std::min (dur, m.ticksPerBar - off);

            int idx = tn.degree + shift;

            while (idx < lo) idx += n;
            while (idx > hi) idx -= n;

            plan.rhythms[(size_t) bar].push_back ({ off, clipped, tn.rest });
            plan.indices[(size_t) bar].push_back (tn.rest ? kNoIndex : idx);
            plan.fixed[(size_t) bar].push_back (true);
            tick += dur;
        }

        // Fill the remainder of the bar the theme ends in, drop bars it never reached.
        const int themeEnd = std::min (tick, phraseTicks);
        const int lastBar = themeEnd > 0 ? (themeEnd - 1) / m.ticksPerBar : -1;
        const int endInBar = themeEnd - lastBar * m.ticksPerBar;

        if (lastBar >= 0 && endInBar < m.ticksPerBar)
        {
            const int from = ((endInBar + m.beatTicks - 1) / m.beatTicks) * m.beatTicks;

            if (from < m.ticksPerBar)
            {
                for (auto& r : fillRhythm (m, from, m.ticksPerBar, t.density, style, rng, true))
                {
                    plan.rhythms[(size_t) lastBar].push_back (r);
                    plan.indices[(size_t) lastBar].push_back (kNoIndex);
                    plan.fixed[(size_t) lastBar].push_back (false);
                }
            }
        }

        plan.rhythms.resize ((size_t) (lastBar + 1));
        plan.indices.resize ((size_t) (lastBar + 1));
        plan.fixed.resize ((size_t) (lastBar + 1));
    }

    Notes out;
    int prevIdx = center - 1;
    int prevDelta = 0;
    bool openingLeapDone = ! ctx.spec.theme.empty() && useTheme;

    for (size_t pi = 0; pi < phrases.size(); ++pi)
    {
        const auto& ph = phrases[pi];
        Plan& plan = plans[ph.type];
        const bool repeat = plan.used;

        // A key lift transposes the phrase: indices stay, the scale moves.
        int keyShift = wrapPc (ph.keyRoot - ctx.spec.keyRoot);

        if (keyShift > 6)
            keyShift -= 12;

        const ScaleMap psm { sm.intervals, sm.tonicPitch + keyShift };
        const int plo = psm.indexAtOrAbove (t.lowNote);
        const int phi = std::max (plo + n, psm.indexAtOrBelow (t.highNote));
        const int typeOffset = ph.type == 'B' ? std::max (1, span / 6) : 0;
        const double amp = std::max (2.0, span * (ph.type == 'B' ? 0.32 : 0.26));
        const bool closeCadence = ph.final || pi % 2 == 1;

        for (int b = 0; b < ph.length; ++b)
        {
            const int bs = (ph.startBar + b) * m.ticksPerBar;
            const bool lastBar = b == ph.length - 1;
            const bool finalBar = ph.final && lastBar && ctx.hasFinalBar();

            if ((int) plan.rhythms.size() <= b)
            {
                std::vector<RNote> r;

                if (lastBar && ph.length > 1)
                    r = cadenceRhythm (m, ph.final, ! ph.final && rng.chance (0.5), t.density, style, rng);
                else if (b == 2 && ph.length == 4 && rng.chance (0.6))
                    r = plan.rhythms[0];
                else
                {
                    // Most bars take a rhythm learned from the corpus; the rest keep the hand-written cells.
                    if (rng.chance (0.7))
                        r = learnedBarRhythm (m, t.density, style, ctx.families, b == 0, rng);

                    if (r.empty())
                        r = fillRhythm (m, 0, m.ticksPerBar, t.density, style, rng, b > 0);
                }

                plan.rhythms.push_back (r);
                plan.indices.emplace_back (r.size(), kNoIndex);
                plan.fixed.emplace_back (r.size(), false);
            }

            std::vector<RNote> rhythm = plan.rhythms[(size_t) b];
            bool planned = true;

            if (finalBar)
            {
                rhythm = { { 0, m.ticksPerBar, false } };
                planned = false;
            }
            else if (repeat && lastBar && ph.length > 1 && rng.chance (0.5))
            {
                rhythm = cadenceRhythm (m, ph.final, ! ph.final && rng.chance (0.5), t.density, style, rng);
                planned = false;
            }

            int lastSounding = -1;

            for (int k = 0; k < (int) rhythm.size(); ++k)
                if (! rhythm[(size_t) k].rest)
                    lastSounding = k;

            for (int k = 0; k < (int) rhythm.size(); ++k)
            {
                const auto& r = rhythm[(size_t) k];

                if (r.rest)
                    continue;

                const int tick = bs + r.offset;
                const auto& ch = ctx.chordAt (tick);
                const double tp = ((double) b * m.ticksPerBar + r.offset) / (ph.length * m.ticksPerBar);
                const int target = center + typeOffset + (int) std::lround (amp * (std::sin (kPi * std::min (1.0, tp * 1.15)) - 0.35));
                const bool strong = isStrongPosition (m, r.offset, r.dur) || tick == ch.start;

                int idx = kNoIndex;
                bool fixedNote = false;

                if (planned && b < (int) plan.indices.size() && k < (int) plan.indices[(size_t) b].size())
                {
                    const int stored = plan.indices[(size_t) b][(size_t) k];
                    fixedNote = plan.fixed[(size_t) b][(size_t) k];

                    if (stored != kNoIndex && (repeat || fixedNote))
                    {
                        idx = stored;

                        if (! fixedNote && (strong || r.offset % m.beatTicks == 0))
                            idx = snapToChordTone (psm, idx, plo, phi, ch);
                    }
                }

                if (idx == kNoIndex)
                {
                    // A theme is recognisable by its opening gesture: the first strong note after
                    // the pickup leaps (a 4th or 5th for energetic music, a 3rd to a 6th otherwise).
                    int leapBias = 0;

                    if (! repeat && b == 0 && strong && k > 0 && ! openingLeapDone && ph.type == 'A')
                    {
                        static const int lyricalLeaps[] = { 2, 3, 5 };
                        leapBias = ctx.spec.energy >= 0.6 ? rng.range (3, 4) : lyricalLeaps[rng.range (0, 2)];
                        openingLeapDone = true;
                    }

                    idx = chooseMelodyIndex (prevIdx, prevDelta, target, plo, phi, ch, strong,
                                             style == MelStyle::Rhythmic, rng, psm, leapBias, r.offset % m.beatTicks == 0);

                    if (lastBar && k == lastSounding && ! fixedNote)
                    {
                        const auto pref = finalBar ? CadencePref::Tonic
                                                   : (closeCadence ? CadencePref::Root : CadencePref::Open);
                        idx = cadenceIndex (psm, prevIdx, plo, phi, ch, pref, ph.keyRoot);
                    }

                    if (planned && ! repeat && b < (int) plan.indices.size() && k < (int) plan.indices[(size_t) b].size())
                        plan.indices[(size_t) b][(size_t) k] = idx;
                }

                if (finalBar)
                    idx = cadenceIndex (psm, prevIdx, plo, phi, ch, CadencePref::Tonic, ph.keyRoot);

                // Stay playable: fold anything outside the part's register back by an octave.
                while (idx > phi) idx -= n;
                while (idx < plo) idx += n;

                RawNote note;
                note.start = tick;
                note.length = std::max (kStep / 2, (int) std::lround (r.dur * legato));
                note.pitch = psm.pitch (idx);
                note.velocity = t.velocity * ctx.energyAt (tick) * (strong ? 1.06 : 0.94)
                              * (1.0 + 0.1 * std::sin (kPi * tp)) * (finalBar ? 0.95 : 1.0);
                out.push_back (note);

                prevDelta = idx - prevIdx;
                prevIdx = idx;
            }
        }

        plan.used = true;
    }

    return out;
}

//==============================================================================
int melodyPitchAt (const Notes& melody, int tick)
{
    for (auto& n : melody)
        if (n.start <= tick && tick < n.start + n.length)
            return n.pitch;

    return -1;
}

/** Doubles the lead melody in this part's register (violins an octave above the horns, etc.). */
Notes doubleMelody (const Context& ctx, const TrackSpec& t)
{
    Notes out;
    const int from = ctx.partStart (t);
    const int to = ctx.partEnd (t);
    double sum = 0.0;
    int count = 0;

    for (auto& n : ctx.leadMelody)
        if (n.start >= from && n.start < to)
        {
            sum += n.pitch;
            ++count;
        }

    if (count == 0)
        return out;

    const double centre = (t.lowNote + t.highNote) * 0.5;
    const int shift = 12 * (int) std::lround ((centre - sum / count) / 12.0);

    for (auto n : ctx.leadMelody)
    {
        if (n.start < from || n.start >= to)
            continue;

        n.pitch += shift;

        while (n.pitch > t.highNote) n.pitch -= 12;
        while (n.pitch < t.lowNote)  n.pitch += 12;

        n.velocity = t.velocity * ctx.energyAt (n.start) * 0.95;
        out.push_back (n);
    }

    return out;
}

Notes generateCounterMelody (Context& ctx, const TrackSpec& t, Rng& rng)
{
    if (t.pattern == "double" && ! ctx.leadMelody.empty())
        return doubleMelody (ctx, t);

    const Meter& m = ctx.meter;
    Notes out;

    int perChord = t.density < 0.35 ? 1 : (t.density < 0.65 ? 2 : 4);

    if (t.pattern == "sustained") perChord = 1;
    if (t.pattern == "flowing")   perChord = std::max (4, perChord * 2);

    int prev = (t.lowNote + t.highNote) / 2;

    for (auto& ch : ctx.chordsBetween (ctx.partStart (t), ctx.partEnd (t)))
    {
        const int len = ch.end - ch.start;
        int segments = std::max (1, perChord);

        // Keep segments on beat boundaries.
        while (segments > 1 && (len / segments) % m.beatTicks != 0)
            --segments;

        if (ctx.inFinalBar (ch.start))
            segments = 1;

        const int segLen = len / segments;

        for (int sIdx = 0; sIdx < segments; ++sIdx)
        {
            const int s = ch.start + sIdx * segLen;

            if (t.density < 0.25 && sIdx > 0 && rng.chance (0.3))
                continue;

            const int mel = melodyPitchAt (ctx.leadMelody, s);
            const int melBefore = melodyPitchAt (ctx.leadMelody, std::max (0, s - m.beatTicks));
            const int melDir = (mel >= 0 && melBefore >= 0) ? (mel > melBefore ? 1 : (mel < melBefore ? -1 : 0)) : 0;

            int best = -1;
            double bestScore = 1e9;

            for (int p = t.lowNote; p <= t.highNote; ++p)
            {
                const int pc = wrapPc (p);

                if (! ch.chord.containsPc (pc))
                    continue;

                const int iv = wrapPc (pc - ch.chord.rootPc);
                double sc = std::abs (p - prev) * 0.4;

                if (iv == 3 || iv == 4 || iv == 10 || iv == 11)  sc -= 1.2; // guide tones
                else if (iv != 0)                                sc -= 0.3;

                if (mel >= 0)
                {
                    if (wrapPc (mel) == pc)       sc += 3.0;
                    if (std::abs (p - mel) < 3)   sc += 2.0;
                    if (p > mel)                  sc += 4.0;
                }

                const int dir = p > prev ? 1 : (p < prev ? -1 : 0);

                if (dir != 0 && dir == melDir)
                    sc += 0.8;

                sc += rng.uniform (0.0, 0.6);

                if (sc < bestScore)
                {
                    bestScore = sc;
                    best = p;
                }
            }

            if (best < 0)
                continue;

            RawNote note;
            note.start = s;
            note.length = (int) (segLen * 0.98);
            note.pitch = best;
            note.velocity = t.velocity * ctx.energyAt (s) * (sIdx == 0 ? 1.0 : 0.92);
            out.push_back (note);
            prev = best;
        }
    }

    return out;
}

//==============================================================================
// Chords & pads

std::string defaultChordPattern (const Context& ctx, const TrackSpec& t)
{
    const int p = t.program;

    if (ctx.meter.beatsPerBar == 3 && ! ctx.meter.compound && (isPianoLike (p) || (p >= 40 && p <= 51)))
        return "waltz";

    if (p == 29 || p == 30)        return "power";
    if (p == 4 || p == 5 || p == 11) return "comp";
    if (isPianoLike (p))           return t.density < 0.6 ? "broken" : "pulse";
    if (isGuitarLike (p))          return "strum";
    if (p == 46 || p == 45)        return "broken";

    return t.density < 0.4 ? "sustain" : "block";
}

void addHit (Notes& out, const std::vector<int>& pitches, int start, int length, double velocity, int rollTicks, bool descending)
{
    for (size_t i = 0; i < pitches.size(); ++i)
    {
        RawNote n;
        n.start = start;
        n.length = std::max (kStep / 2, length);
        n.pitch = pitches[descending ? pitches.size() - 1 - i : i];
        n.velocity = velocity * (1.0 - 0.03 * (double) i);
        n.micro = (int) i * rollTicks;
        out.push_back (n);
    }
}

void generateChords (Context& ctx, const TrackSpec& t, Rng& rng, Notes& out, Controls& cc, bool isPad)
{
    const Meter& m = ctx.meter;
    const std::string pattern = t.pattern == "auto" ? (isPad ? std::string ("sustain") : defaultChordPattern (ctx, t)) : t.pattern;
    const bool comp = pattern == "comp";
    const int voices = (pattern == "strum") ? 5 : (comp ? 3 : 4);
    const int maxSpread = (pattern == "strum") ? 26 : (isPad ? 24 : 19);
    const int roll = isPianoLike (t.program) || isGuitarLike (t.program) || t.program == 46
                   ? (int) std::lround (4 + 8 * ctx.spec.humanize) : 0;

    // Per-piece rhythmic choices, so every bar of a stab/bossa part grooves the same way.
    static const std::vector<std::vector<int>> stabRhythms = { { 0, 3, 6, 10, 12 }, { 0, 6, 8, 14 }, { 3, 6, 10, 13 }, { 0, 3, 8, 11, 14 } };
    const auto& stab = stabRhythms[(size_t) rng.range (0, (int) stabRhythms.size() - 1)];

    // Block chords: half the time a comping groove instead of even hits ({position, length} in 16ths).
    static const std::vector<std::vector<int>> blockGrooves = {
        { 0, 6, 6, 6, 12, 4 },          // 3-3-2
        { 0, 6, 6, 10 },                // Charleston
        { 0, 10, 10, 6 },               // anticipated third beat
        { 0, 4, 4, 4, 8, 4, 12, 4 },    // four on the floor
        { 0, 8, 10, 2, 12, 4 },         // half, then a push
        { 0, 3, 6, 4, 12, 4 } };        // short-long
    const bool groove = rng.chance (0.5) && t.density >= 0.35;
    const auto* blockGroove = groove ? &blockGrooves[(size_t) rng.range (0, (int) blockGrooves.size() - 1)] : nullptr;

    std::vector<int> prevVoicing;

    // Sustained harmony sits above the bass line; close chords below C3 turn to mud.
    int lowNote = t.lowNote, highNote = t.highNote;

    if (ctx.hasBassTrack && (isPad || pattern == "sustain" || pattern == "block" || pattern == "swell") && lowNote < 48)
    {
        int instLo = 0, instHi = 127;
        instrumentRange (t.program, instLo, instHi);
        lowNote = 48;
        highNote = std::min (instHi, std::max (highNote, lowNote + 19));
    }

    for (auto& ch : ctx.chordsBetween (ctx.partStart (t), ctx.partEnd (t)))
    {
        const auto wanted = wantedPcs (ch.chord, voices, comp);
        const auto voicing = voiceChord (wanted, ch.chord.rootPc, lowNote, highNote, prevVoicing, maxSpread, false);
        prevVoicing = voicing;

        const int s = ch.start, e = ch.end, len = e - s;
        const double baseVel = t.velocity * ctx.energyAt (s);

        if (ctx.inFinalBar (s))
        {
            addHit (out, voicing, s, len, baseVel * 0.95, roll > 0 ? roll * 3 : 0);
            continue;
        }

        if (pattern == "sustain" || pattern == "swell")
        {
            addHit (out, voicing, s, len, baseVel * (pattern == "swell" ? 0.85 : 0.9), roll);

            if (pattern == "swell")
            {
                // An expression swell per chord: CC1 (orchestral libraries) and CC11 (General MIDI).
                const int points = 8;

                for (int i = 0; i <= points; ++i)
                {
                    const double x = (double) i / points;
                    const double shape = x < 0.6 ? x / 0.6 : 1.0 - (x - 0.6) / 0.4 * 0.45;
                    const int value = clampv ((int) std::lround (45 + 65 * shape), 0, 127);
                    const int tick = s + (int) (x * (len - 1));
                    cc.push_back ({ tick, 1, value });
                    cc.push_back ({ tick, 11, value });
                }
            }

            continue;
        }

        const int beat = m.beatTicks;
        const int sub = m.subdivTicks;

        if (pattern == "block" && blockGroove != nullptr && len == m.ticksPerBar && m.beatsPerBar == 4 && ! m.compound)
        {
            // This piece's comping rhythm (16th positions and lengths), the same every bar.
            for (size_t k = 0; k < blockGroove->size(); k += 2)
            {
                const int x = s + (*blockGroove)[k] * kStep;
                addHit (out, voicing, x, (int) ((*blockGroove)[k + 1] * kStep * 0.92), baseVel * (k == 0 ? 1.0 : 0.86), roll);
            }
        }
        else if (pattern == "block")
        {
            int step = t.density < 0.3 ? len : (t.density < 0.6 ? beat * 2 : beat);

            if (m.compound)
                step = t.density < 0.45 ? len : beat;

            step = std::max (beat, std::min (step, len));

            for (int x = s; x < e; x += step)
                addHit (out, voicing, x, (int) (std::min (step, e - x) * 0.92), baseVel * (x == s ? 1.0 : 0.88), roll);
        }
        else if (pattern == "pulse")
        {
            const int step = t.density >= 0.5 ? sub : beat;

            for (int x = s; x < e; x += step)
            {
                const bool onBeat = (x - s) % beat == 0;
                addHit (out, voicing, x, (int) (step * 0.8), baseVel * (onBeat ? 1.0 : 0.8));
            }
        }
        else if (pattern == "broken")
        {
            const int bassNote = bassPitchFor (ch.bassRoot(), t.lowNote, std::min (t.highNote, t.lowNote + 14), -1);
            std::vector<int> sweep { bassNote };

            for (int p = bassNote + 1; p <= std::min (t.highNote, bassNote + 20) && sweep.size() < 5; ++p)
                if (ch.chord.containsPc (p) && p - sweep.back() >= 3)
                    sweep.push_back (p);

            std::vector<int> seq;

            for (size_t i = 0; i < sweep.size(); ++i)            seq.push_back ((int) i);
            for (int i = (int) sweep.size() - 2; i >= 1; --i)   seq.push_back (i);

            const int step = t.density < 0.4 ? beat : sub;
            int k = 0;

            for (int x = s; x < e; x += step, ++k)
            {
                // restart the sweep at every bar line
                if ((x - s) % m.ticksPerBar == 0)
                    k = 0;

                const int idx = seq[(size_t) (k % (int) seq.size())];
                RawNote n;
                n.start = x;
                n.length = std::min (e - x, beat * 2);
                n.pitch = sweep[(size_t) idx];
                n.velocity = baseVel * (idx == 0 ? 1.0 : 0.8);
                out.push_back (n);
            }
        }
        else if (pattern == "alberti")
        {
            auto v = voicing;

            while (v.size() > 3)
                v.erase (v.begin() + 1);

            const int lowN = v.front(), midN = v.size() > 2 ? v[1] : v.front() + 7, highN = v.back();
            const int order[] = { lowN, highN, midN, highN };
            const int step = t.density > 0.7 ? kStep : sub;
            int k = 0;

            for (int x = s; x < e; x += step, ++k)
            {
                RawNote n;
                n.start = x;
                n.length = (int) (step * 0.9);
                n.pitch = order[k % 4];
                n.velocity = baseVel * (k % 4 == 0 ? 0.95 : 0.78);
                out.push_back (n);
            }
        }
        else if (pattern == "strum")
        {
            // 'D' = down strum, 'U' = up strum, positions in subdivisions
            std::vector<std::pair<int, char>> hits;

            if (m.compound)               hits = { { 0, 'D' }, { 2, 'U' }, { 3, 'D' }, { 5, 'U' } };
            else if (m.beatsPerBar == 3)  hits = { { 0, 'D' }, { 2, 'D' }, { 3, 'U' }, { 4, 'D' }, { 5, 'U' } };
            else if (m.beatsPerBar == 2)  hits = { { 0, 'D' }, { 2, 'D' }, { 3, 'U' } };
            else if (t.density < 0.45)    hits = { { 0, 'D' }, { 4, 'D' } };
            else                          hits = { { 0, 'D' }, { 2, 'D' }, { 3, 'U' }, { 5, 'U' }, { 6, 'D' }, { 7, 'U' } };

            const int subsPerBar = m.ticksPerBar / sub;

            for (int barStart = s; barStart < e; barStart += m.ticksPerBar)
            {
                for (size_t h = 0; h < hits.size(); ++h)
                {
                    const int x = barStart + hits[h].first * sub;

                    if (x >= e)
                        continue;

                    const int nextPos = h + 1 < hits.size() ? hits[h + 1].first : subsPerBar;
                    const int length = std::min (e - x, (nextPos - hits[h].first) * sub) - 10;
                    const bool down = hits[h].second == 'D';
                    const int strumSpread = (int) std::lround (8 + 10 * ctx.spec.humanize);

                    if (down)
                    {
                        addHit (out, voicing, x, length, baseVel * (h == 0 ? 1.0 : 0.88), strumSpread);
                    }
                    else
                    {
                        std::vector<int> top (voicing.end() - std::min ((size_t) 3, voicing.size()), voicing.end());
                        addHit (out, top, x, length, baseVel * 0.68, strumSpread, true);
                    }
                }
            }
        }
        else if (pattern == "stabs")
        {
            for (int barStart = s; barStart < e; barStart += m.ticksPerBar)
                for (int pos : stab)
                {
                    const int x = barStart + pos * kStep;

                    if (x < e && pos < m.stepsPerBar)
                        addHit (out, voicing, x, (int) (kStep * 1.5), baseVel * (pos % 4 == 0 ? 1.0 : 0.9));
                }
        }
        else if (pattern == "offbeat")
        {
            if (t.density < 0.5 && ! m.compound && m.beatsPerBar == 4)
            {
                for (int x = s + beat; x < e; x += beat * 2)
                    addHit (out, voicing, x, (int) (sub * 0.7), baseVel * 0.9);
            }
            else
            {
                for (int x = s + sub; x < e; x += m.compound ? sub * 3 / 2 : sub * 2)
                    addHit (out, voicing, x, (int) (sub * 0.6), baseVel * 0.9);
            }
        }
        else if (pattern == "comp")
        {
            const int subsPerBar = m.ticksPerBar / sub;
            static const double weights8[] = { 1.0, 0.8, 0.45, 1.0, 0.6, 0.9, 0.4, 1.0 };

            for (int barStart = s; barStart < e; barStart += m.ticksPerBar)
            {
                const int hitsWanted = clampv ((int) std::lround (1.5 + t.density * 3.0) + rng.range (-1, 1), 1, 4);
                std::vector<int> chosen;

                for (int attempt = 0; attempt < 24 && (int) chosen.size() < hitsWanted; ++attempt)
                {
                    const int pos = rng.range (0, subsPerBar - 1);
                    const double w = weights8[pos % 8];

                    if (std::find (chosen.begin(), chosen.end(), pos) == chosen.end() && rng.chance (w))
                        chosen.push_back (pos);
                }

                std::sort (chosen.begin(), chosen.end());

                for (size_t h = 0; h < chosen.size(); ++h)
                {
                    const int x = barStart + chosen[h] * sub;

                    if (x >= e)
                        continue;

                    const int next = h + 1 < chosen.size() ? chosen[h + 1] : subsPerBar;
                    const int length = std::min (e - x, std::min (next - chosen[h], rng.range (1, 3)) * sub) - 10;
                    addHit (out, voicing, x, length, baseVel * rng.uniform (0.72, 0.95), roll);
                }
            }
        }
        else if (pattern == "waltz")
        {
            const int bassNote = bassPitchFor (ch.bassRoot(), t.lowNote, std::min (t.highNote, t.lowNote + 14), -1);
            std::vector<int> upper;

            for (int p : voicing)
                if (p > bassNote + 4)
                    upper.push_back (p);

            if (upper.size() < 2)
                upper = voicing;

            for (int barStart = s; barStart < e; barStart += m.ticksPerBar)
            {
                if (m.compound)
                {
                    for (int i = 0; i < m.beatsPerBar * 3; ++i)
                    {
                        const int x = barStart + i * sub;

                        if (x >= e) break;

                        if (i % 3 == 0) addHit (out, { i == 3 ? fifthAbove (bassNote, t.highNote) : bassNote }, x, (int) (sub * 0.95), baseVel);
                        else            addHit (out, upper, x, (int) (sub * 0.6), baseVel * 0.72);
                    }
                }
                else
                {
                    for (int b = 0; b < m.beatsPerBar; ++b)
                    {
                        const int x = barStart + b * beat;

                        if (x >= e) break;

                        const bool bassBeat = (m.beatsPerBar == 3) ? b == 0 : b % 2 == 0;

                        if (bassBeat)
                            addHit (out, { b == 2 ? fifthAbove (bassNote, t.highNote) : bassNote }, x, (int) (beat * 0.95), baseVel);
                        else
                            addHit (out, upper, x, (int) (beat * 0.55), baseVel * 0.72);
                    }
                }
            }
        }
        else if (pattern == "power")
        {
            const int root = bassPitchFor (ch.bassRoot(), t.lowNote, std::min (t.highNote, t.lowNote + 12), -1);
            const std::vector<int> power { root, root + 7, root + 12 };
            const int step = t.density >= 0.5 ? sub : beat;

            for (int x = s; x < e; x += step)
            {
                const bool onBeat = (x - s) % beat == 0;
                addHit (out, power, x, (int) (step * (t.density > 0.8 ? 0.6 : 0.85)), baseVel * (onBeat ? 1.0 : 0.85));
            }
        }
        else if (pattern == "bossa")
        {
            static const int barA[] = { 0, 3, 6, 10, 12 };
            static const int barB[] = { 2, 6, 8, 12, 14 };
            for (int barStart = s; barStart < e; barStart += m.ticksPerBar)
            {
                const int* positions = ((barStart / m.ticksPerBar) % 2 == 0) ? barA : barB;

                for (int i = 0; i < 5; ++i)
                {
                    const int x = barStart + positions[i] * kStep;

                    if (x < e && positions[i] < m.stepsPerBar)
                        addHit (out, voicing, x, (int) (kStep * 1.8), baseVel * (i == 0 ? 0.95 : 0.85), 3);
                }
            }
        }
        else
        {
            addHit (out, voicing, s, len, baseVel, roll);
        }
    }
}

//==============================================================================
Notes generateArpeggio (Context& ctx, const TrackSpec& t, Rng& rng)
{
    const Meter& m = ctx.meter;
    Notes out;

    std::string pattern = t.pattern == "auto" ? (t.program == 46 ? "updown" : "up") : t.pattern;

    int step;

    if (m.compound)        step = t.density >= 0.75 ? kStep : m.subdivTicks;
    else                   step = t.density < 0.35 ? m.beatTicks : (t.density < 0.7 ? m.subdivTicks : kStep);

    const double lengthFactor = isRinging (t.program) ? 1.8 : 0.85;
    const size_t maxPool = t.program == 46 ? 10 : 7;

    // With a bass part underneath, arpeggios start above C3 so they don't muddy it.
    int lowNote = t.lowNote, highNote = t.highNote;

    if (ctx.hasBassTrack && lowNote < 48)
    {
        int instLo = 0, instHi = 127;
        instrumentRange (t.program, instLo, instHi);
        lowNote = 48;
        highNote = std::min (instHi, std::max (highNote, lowNote + 19));
    }

    const double target = lowNote + (highNote - lowNote) * 0.4;

    for (auto& ch : ctx.chordsBetween (ctx.partStart (t), ctx.partEnd (t)))
    {
        std::vector<int> pool;

        for (int p = lowNote; p <= highNote; ++p)
            if (ch.chord.containsPc (p))
                pool.push_back (p);

        if (pool.empty())
            continue;

        if (pool.size() > maxPool)
        {
            size_t bestStart = 0;
            double bestDist = 1e9;

            for (size_t st = 0; st + maxPool <= pool.size(); ++st)
            {
                const double d = std::abs (pool[st + maxPool / 2] - target);

                if (d < bestDist)
                {
                    bestDist = d;
                    bestStart = st;
                }
            }

            pool = std::vector<int> (pool.begin() + (std::ptrdiff_t) bestStart, pool.begin() + (std::ptrdiff_t) (bestStart + maxPool));
        }

        const double baseVel = t.velocity * ctx.energyAt (ch.start);

        if (ctx.inFinalBar (ch.start))
        {
            // One last rolled chord that rings out.
            for (size_t i = 0; i < pool.size(); ++i)
            {
                RawNote n;
                n.start = ch.start + (int) i * (kStep / 2);
                n.length = ch.end - n.start;
                n.pitch = pool[i];
                n.velocity = baseVel * (0.8 + 0.2 * (double) i / pool.size());
                out.push_back (n);
            }

            continue;
        }

        const int np = (int) pool.size();
        int walk = 0;

        for (int x = ch.start, k = 0; x < ch.end; x += step, ++k)
        {
            int idx = 0;

            if (pattern == "down")
            {
                idx = np - 1 - (k % np);
            }
            else if (pattern == "updown")
            {
                const int period = std::max (1, 2 * np - 2);
                const int ph = k % period;
                idx = ph < np ? ph : period - ph;
            }
            else if (pattern == "random")
            {
                walk = clampv (walk + rng.range (-2, 2), 0, np - 1);

                if (k == 0)
                    walk = 0;

                idx = walk;
            }
            else
            {
                idx = k % np;
            }

            RawNote n;
            n.start = x;
            n.length = std::min ((int) (step * lengthFactor), ch.end - x);
            n.pitch = pool[(size_t) idx];
            const bool onBeat = (x - ch.start) % m.beatTicks == 0;
            n.velocity = baseVel * (onBeat ? 1.0 : 0.84) * (0.94 + 0.08 * (double) idx / np);
            out.push_back (n);
        }
    }

    return out;
}

//==============================================================================
Notes generateBass (Context& ctx, const TrackSpec& t, Rng& rng)
{
    const Meter& m = ctx.meter;
    Notes out;

    std::string pattern = t.pattern;

    if (pattern == "auto")
    {
        if (t.program == 47)                                    pattern = "timpani";
        else if (m.beatsPerBar == 3 && ! m.compound)            pattern = "root";
        else if (t.density < 0.35)                              pattern = "sustain";
        else if (t.density < 0.6)                               pattern = "root";
        else                                                    pattern = "pedal";
    }

    if (pattern == "boogie" && (m.compound || m.beatsPerBar != 4))
        pattern = "rootfifth";

    static const std::vector<std::vector<int>> syncopations = { { 0, 6, 10 }, { 0, 3, 8, 10, 14 }, { 0, 7, 10 }, { 0, 6, 8, 11, 14 } };
    const auto& sync = syncopations[(size_t) rng.range (0, (int) syncopations.size() - 1)];
    static const std::vector<int> trapKick = { 0, 7, 10 };

    const int beat = m.beatTicks;
    const int sub = m.subdivTicks;
    int prev = -1;

    auto emit = [&] (int start, int length, int pitch, double vel)
    {
        RawNote n;
        n.start = start;
        n.length = std::max (kStep / 2, length);
        n.pitch = pitch;
        n.velocity = vel;
        out.push_back (n);
    };

    const auto chords = ctx.chordsBetween (ctx.partStart (t), ctx.partEnd (t));

    for (size_t ci = 0; ci < chords.size(); ++ci)
    {
        const auto& ch = chords[ci];
        const int s = ch.start, e = ch.end, len = e - s;
        const int root = bassPitchFor (ch.bassRoot(), t.lowNote, t.highNote, prev);
        prev = root;
        const int fifth = fifthAbove (root, t.highNote);
        const double v = t.velocity * ctx.energyAt (s);

        if (ctx.inFinalBar (s))
        {
            emit (s, len, root, v);
            continue;
        }

        if (pattern == "sustain")
        {
            emit (s, len - 5, root, v);
        }
        else if (pattern == "root" || pattern == "rootfifth" || pattern == "waltz")
        {
            int step;

            if (m.compound)                     step = beat;
            else if (m.beatsPerBar == 3)        step = m.ticksPerBar;
            else                                step = t.density > 0.65 ? beat : beat * 2;

            if (pattern == "waltz")
                step = m.ticksPerBar;

            step = std::min (step, len);
            int k = 0;

            for (int x = s; x < e; x += step, ++k)
            {
                const bool useFifth = (pattern == "rootfifth" && k % 2 == 1) || (pattern == "waltz" && ((x / m.ticksPerBar) % 2 == 1));
                emit (x, (int) (std::min (step, e - x) * 0.92), useFifth ? fifth : root, v * (k == 0 ? 1.0 : 0.9));
            }
        }
        else if (pattern == "octave")
        {
            const int high = root + 12;
            int k = 0;

            for (int x = s; x < e; x += sub, ++k)
                emit (x, (int) (sub * 0.7), k % 2 == 0 ? root : high, v * (k % 2 == 0 ? 1.0 : 0.85));
        }
        else if (pattern == "offbeat")
        {
            for (int x = s + sub; x < e; x += sub * 2)
                emit (x, (int) (sub * 0.75), root, v);
        }
        else if (pattern == "pedal")
        {
            const int step = t.density > 0.8 ? kStep : sub;

            for (int x = s; x < e; x += step)
                emit (x, (int) (step * 0.85), root, v * ((x - s) % beat == 0 ? 1.0 : 0.82));
        }
        else if (pattern == "walking")
        {
            const ChordEvent* next = ci + 1 < chords.size() ? &chords[ci + 1] : nullptr;
            const int nextRoot = next ? bassPitchFor (next->bassRoot(), t.lowNote, t.highNote, root) : root;
            const int beats = std::max (1, len / beat);
            const int third = ch.chord.hasInterval (3) ? 3 : 4;
            const int seventh = ch.chord.hasInterval (11) ? 11 : (ch.chord.hasInterval (9) && ! ch.chord.hasInterval (10) ? 9 : 10);

            // Classic walking shapes: up through the chord when heading up, down when heading down.
            const bool ascending = nextRoot >= root || root - 12 < t.lowNote;
            const int top = ch.chord.hasSeventh() ? seventh : 9;
            const int upLine[]   = { 0, third, 7, top, 12, 7, third, 2 };
            const int downLine[] = { 0, -2, -5, -3, -7, -5, -2, -1 };

            for (int b = 0; b < beats; ++b)
            {
                int p;

                if (b == beats - 1 && beats > 1)
                {
                    p = nextRoot + (rng.chance (0.6) ? -1 : 1); // chromatic approach

                    if (rng.chance (0.25))
                        p = fifthAbove (nextRoot, t.highNote) - 12 >= t.lowNote ? fifthAbove (nextRoot, t.highNote) - 12 : nextRoot + 2;
                }
                else
                {
                    p = root + (ascending ? upLine[b % 8] : downLine[b % 8]);
                }

                while (p < t.lowNote)  p += 12;
                while (p > t.highNote) p -= 12;

                emit (s + b * beat, (int) (beat * 0.9), p, v * (b == 0 ? 1.0 : 0.88));
            }
        }
        else if (pattern == "syncopated" || pattern == "808" || pattern == "bossa")
        {
            std::vector<int> positions = pattern == "808" ? trapKick : sync;
            std::vector<int> pitches (positions.size(), root);

            if (pattern == "bossa")
            {
                positions = { 0, 6, 8, 14 };
                pitches = { root, fifth, fifth, root };
            }
            else if (pattern == "syncopated")
            {
                for (size_t i = 1; i < positions.size(); ++i)
                    if (positions[i] >= 8 && rng.chance (0.3))
                        pitches[i] = rng.chance (0.5) ? fifth : std::min (root + 12, t.highNote);
            }

            for (int barStart = s; barStart < e; barStart += m.ticksPerBar)
            {
                for (size_t i = 0; i < positions.size(); ++i)
                {
                    if (positions[i] >= m.stepsPerBar)
                        continue;

                    const int x = barStart + positions[i] * kStep;

                    if (x >= e)
                        continue;

                    const int nextPos = i + 1 < positions.size() ? positions[i + 1] : m.stepsPerBar;
                    const double lenFactor = pattern == "808" ? 0.97 : 0.85;
                    const int length = (int) (std::min ((nextPos - positions[i]) * kStep, e - x) * lenFactor);
                    emit (x, length, pitches[i], v * (i == 0 ? 1.0 : 0.9));
                }
            }
        }
        else if (pattern == "timpani")
        {
            for (int barStart = s; barStart < e; barStart += m.ticksPerBar)
            {
                emit (barStart, (int) (beat * 0.8), root, v);

                if (t.density >= 0.5 && m.beatsPerBar >= 4)
                    emit (barStart + 2 * beat, (int) (beat * 0.8), rng.chance (0.5) ? root : fifth, v * 0.85);

                const int barIndex = barStart / m.ticksPerBar;

                if ((barIndex + 1) % 4 == 0 && barIndex + 1 < ctx.bars)
                {
                    const int rollStart = barStart + m.ticksPerBar - beat;

                    for (int r = 0; r < beat / kStep; ++r)
                        emit (rollStart + r * kStep, (int) (kStep * 0.9), root, v * (0.6 + 0.12 * r));
                }
            }
        }
        else if (pattern == "boogie")
        {
            const bool minor = ch.chord.hasInterval (3);
            const int line[] = { 0, minor ? 3 : 4, 7, 9, 10, 9, 7, minor ? 3 : 4 };
            int k = 0;

            for (int x = s; x < e; x += sub, ++k)
                emit (x, (int) (sub * 0.85), root + line[k % 8], v * (k % 2 == 0 ? 1.0 : 0.85));
        }
        else
        {
            emit (s, len - 5, root, v);
        }
    }

    return out;
}

//==============================================================================
Notes generateOstinato (Context& ctx, const TrackSpec& t, Rng&)
{
    const Meter& m = ctx.meter;
    Notes out;
    const std::string pattern = t.pattern == "auto" ? "epic" : t.pattern;
    int prev = -1;

    for (auto& ch : ctx.chordsBetween (ctx.partStart (t), ctx.partEnd (t)))
    {
        const int root = bassPitchFor (ch.bassRoot(), t.lowNote, std::min (t.highNote, t.lowNote + 12), prev);
        prev = root;
        const int fifth = root + 7;
        const int octave = root + 12;
        const double v = t.velocity * ctx.energyAt (ch.start);

        auto emit = [&] (int start, int length, int pitch, double vel)
        {
            RawNote n;
            n.start = start;
            n.length = std::max (kStep / 2, length);
            n.pitch = clampv (pitch, 0, 127);
            n.velocity = vel;
            out.push_back (n);
        };

        if (ctx.inFinalBar (ch.start))
        {
            emit (ch.start, ch.end - ch.start, root, v * 1.05);
            emit (ch.start, ch.end - ch.start, octave, v);
            continue;
        }

        if (pattern == "pulse")
        {
            if (t.program == 44) // tremolo strings already tremble; hold the notes
            {
                emit (ch.start, ch.end - ch.start, octave, v);
                emit (ch.start, ch.end - ch.start, fifth + 12 <= t.highNote ? fifth + 12 : fifth, v * 0.9);
                continue;
            }

            for (int x = ch.start; x < ch.end; x += m.subdivTicks)
            {
                emit (x, (int) (m.subdivTicks * 0.7), octave, v * 0.9);
                emit (x, (int) (m.subdivTicks * 0.7), fifth, v * 0.8);
            }
        }
        else if (pattern == "gallop")
        {
            for (int x = ch.start; x < ch.end; x += m.beatTicks)
            {
                const int b = (x - ch.start) / m.beatTicks;
                const int p = (b % 4 == 2) ? fifth : root;

                if (m.compound)
                {
                    emit (x, (int) (m.subdivTicks * 1.6), p, v);
                    emit (x + 2 * m.subdivTicks, (int) (m.subdivTicks * 0.7), p, v * 0.8);
                }
                else
                {
                    emit (x, (int) (m.subdivTicks * 0.6), p, v);
                    emit (x + m.subdivTicks, (int) (kStep * 0.6), p, v * 0.8);
                    emit (x + m.subdivTicks + kStep, (int) (kStep * 0.6), p, v * 0.85);
                }
            }
        }
        else // epic: driving spiccato with 3-3-2 accents
        {
            const bool sixteenths = t.density >= 0.65 && ! m.compound;
            const int step = sixteenths ? kStep : m.subdivTicks;
            const int seq[] = { root, root, fifth, root, root, fifth, octave, fifth };

            for (int x = ch.start, k = 0; x < ch.end; x += step, ++k)
            {
                const int inGroup = sixteenths ? k % 16 : k % 8;
                const bool accent = sixteenths ? (inGroup == 0 || inGroup == 3 || inGroup == 6 || inGroup == 8 || inGroup == 11 || inGroup == 14)
                                               : (inGroup == 0 || inGroup == 3 || inGroup == 6);
                emit (x, (int) (step * 0.6), seq[k % 8], v * (accent ? 1.12 : 0.78));
            }
        }
    }

    return out;
}

//==============================================================================
// Drums

struct Lane
{
    int note;
    std::string steps; // 'X' accent, 'x' normal, 'o' ghost, '.' rest; one char per 16th
    double minDensity;
};

struct DrumPattern
{
    std::vector<Lane> main;
    std::vector<Lane> alt;  // replaces lanes with the same note on odd bars
    int fill = 1;           // 0 none, 1 kit, 2 orchestral roll, 3 toms
    bool crash = true;
};

DrumPattern fourFourPattern (const std::string& id)
{
    static const std::map<std::string, DrumPattern> patterns = {
        { "rock",          { { { 36, "x.......x.x.....", 0 }, { 38, "....x.......x...", 0 }, { 42, "x.x.x.x.x.x.x.x.", 0 } },
                             { { 36, "x.....x.x.......", 0 } }, 1, true } },
        { "pop",           { { { 36, "x.....x.x.......", 0 }, { 38, "....x.......x...", 0 }, { 39, "....x.......x...", 0.65 },
                               { 42, "x.x.x.x.x.x.x.x.", 0 }, { 46, "..............x.", 0.4 } },
                             { { 36, "x.....x...x.....", 0 } }, 1, true } },
        { "synthwave",     { { { 36, "x.......x.......", 0 }, { 38, "....x.......x...", 0 }, { 39, "....x.......x...", 0.4 },
                               { 42, "x.x.x.x.x.x.x.x.", 0 } },
                             { { 36, "x.......x.x.....", 0 } }, 1, true } },
        { "four_on_floor", { { { 36, "x...x...x...x...", 0 }, { 39, "....x.......x...", 0 }, { 46, "..x...x...x...x.", 0 },
                               { 42, ".o.o.o.o.o.o.o.o", 0.55 } },
                             {}, 1, true } },
        { "hiphop",        { { { 36, "x......x..x.....", 0 }, { 38, "....x.......x...", 0 }, { 42, "x.o.x.o.x.o.x.o.", 0 } },
                             { { 36, "x.........xx....", 0 } }, 1, false } },
        { "trap",          { { { 36, "x......x..x.....", 0 }, { 39, "........x.......", 0 }, { 38, "........x.......", 0.5 },
                               { 42, "xoxoxoxoxoxoxoxo", 0 }, { 46, "......x.........", 0.6 } },
                             { { 36, "x.....x...x..x..", 0 } }, 0, false } },
        { "lofi",          { { { 36, "x.......x.x.....", 0 }, { 38, "....x.......x...", 0 }, { 37, "..........o.....", 0.5 },
                               { 42, "x.o.x.o.x.o.x.o.", 0 } },
                             { { 36, "x......xx.......", 0 } }, 0, false } },
        { "jazz",          { { { 51, "x...x.x.x...x.x.", 0 }, { 44, "....x.......x...", 0 }, { 36, "o...o...o...o...", 0.6 } },
                             {}, 0, false } },
        { "bossa",         { { { 37, "x..x..x...x..x..", 0 }, { 36, "x..x....x..x....", 0 }, { 42, "x.o.x.o.x.o.x.o.", 0 } },
                             { { 37, "..x...x...x..x..", 0 } }, 0, false } },
        { "orchestral",    { { { 35, "x.......x.......", 0 }, { 41, "......x...x.....", 0.35 }, { 45, "...x.......x..x.", 0.55 },
                               { 38, "....x.......x...", 0.75 } },
                             { { 41, "......x.....x...", 0.35 } }, 2, true } },
        { "march",         { { { 36, "x.......x.......", 0 }, { 38, "x.oox.x.x.oox.x.", 0 }, { 49, "....x.......x...", 0.5 } },
                             {}, 1, true } },
        { "ambient",       { { { 35, "x...............", 0 }, { 41, "..........o.....", 0.4 } },
                             {}, 0, true } },
        { "breakbeat",     { { { 36, "x.........x.....", 0 }, { 38, "....x.......x...", 0 }, { 38, ".......o.o....o.", 0.5 },
                               { 42, "x.x.x.x.x.x.x.x.", 0 } },
                             { { 36, "x.x.......x.....", 0 } }, 1, true } },
        { "funk",          { { { 36, "x.x.......x..x..", 0 }, { 38, "....x..o.o..x..o", 0 }, { 42, "xoxoxoxoxoxoxoxo", 0 } },
                             { { 36, "x..x..x...x.....", 0 } }, 1, false } },
        { "shuffle",       { { { 36, "x.......x.......", 0 }, { 38, "....x.......x...", 0 }, { 42, "x.x.x.x.x.x.x.x.", 0 } },
                             {}, 1, true } },
        { "metal",         { { { 36, "x.x.x.x.x.x.x.x.", 0 }, { 36, "xxxxxxxxxxxxxxxx", 0.75 }, { 38, "....x.......x...", 0 },
                               { 42, "x.x.x.x.x.x.x.x.", 0 } },
                             {}, 1, true } },
        { "folk",          { { { 45, "x..ox..ox..ox..o", 0 }, { 54, "....x.......x...", 0.3 }, { 36, "x.......x.......", 0.6 } },
                             {}, 3, false } },
        { "tribal",        { { { 35, "x.......x.......", 0 }, { 41, "x..x..x.....x...", 0 }, { 43, "......x...x...x.", 0.45 },
                               { 45, "..x.......x...x.", 0.7 } },
                             {}, 3, true } },
        { "arabic",        { { { 64, "x.......x.......", 0 }, { 62, "..x...x.....x...", 0 }, { 62, "..........o...o.", 0.5 },
                               { 54, "o.o.o.o.o.o.o.o.", 0.6 } },
                             {}, 0, false } },
        { "reggae",        { { { 36, "........x.......", 0 }, { 37, "........x.......", 0 }, { 42, "x.x.x.x.x.x.x.x.", 0 } },
                             {}, 0, false } },
        { "chiptune",      { { { 36, "x.......x.......", 0 }, { 40, "....x.......x...", 0 }, { 42, "x.x.x.x.x.x.x.x.", 0 } },
                             { { 36, "x.....x.x.......", 0 } }, 1, true } },
        { "halftime",      { { { 36, "x.........x.....", 0 }, { 38, "........x.......", 0 }, { 42, "x.x.x.x.x.x.x.x.", 0 } },
                             {}, 1, true } },
    };

    auto it = patterns.find (id);
    return it != patterns.end() ? it->second : patterns.at ("pop");
}

/** Builds a sensible groove for meters other than 4/4. */
DrumPattern genericPattern (const std::string& id, const Meter& m)
{
    const int steps = m.stepsPerBar;
    const int beatSteps = m.beatTicks / kStep;
    const int subSteps = std::max (1, m.subdivTicks / kStep);

    auto blank = [&] { return std::string ((size_t) steps, '.'); };
    auto put = [&] (std::string& s, int step, char c) { if (step >= 0 && step < steps) s[(size_t) step] = c; };

    DrumPattern p;
    const bool orchestral = id == "orchestral" || id == "tribal" || id == "ambient";

    if (orchestral)
    {
        std::string boom = blank(), tom = blank(), low = blank();
        put (boom, 0, 'x');

        if (m.beatsPerBar >= 2)
            put (tom, beatSteps * (m.beatsPerBar - 1), 'x');

        for (int b = 0; b < m.beatsPerBar; ++b)
            put (low, b * beatSteps + beatSteps - subSteps, 'o');

        p.main = { { 35, boom, 0 }, { 41, tom, 0.35 }, { 45, low, 0.6 } };
        p.fill = id == "ambient" ? 0 : (id == "tribal" ? 3 : 2);
        p.crash = true;
        return p;
    }

    if (id == "folk")
    {
        std::string tom = blank(), tamb = blank();

        for (int sIdx = 0; sIdx < steps; sIdx += subSteps)
            put (tom, sIdx, (sIdx % beatSteps == 0) ? 'x' : 'o');

        for (int b = 1; b < m.beatsPerBar; b += 2)
            put (tamb, b * beatSteps, 'x');

        p.main = { { 45, tom, 0 }, { 54, tamb, 0.3 } };
        p.fill = 3;
        p.crash = false;
        return p;
    }

    if (id == "jazz")
    {
        std::string ride = blank(), hat = blank();

        for (int b = 0; b < m.beatsPerBar; ++b)
            put (ride, b * beatSteps, 'x');

        for (int b = 1; b < m.beatsPerBar; b += 2)
            put (hat, b * beatSteps, 'x');

        p.main = { { 51, ride, 0 }, { 44, hat, 0 } };
        p.fill = 0;
        p.crash = false;
        return p;
    }

    std::string kick = blank(), snare = blank(), hat = blank();
    put (kick, 0, 'x');

    if (m.beatsPerBar == 4)
        put (kick, 2 * beatSteps, 'x');

    if (id == "waltz" || (m.beatsPerBar == 3 && ! m.compound))
    {
        put (snare, beatSteps, 'o');
        put (snare, 2 * beatSteps, 'o');
    }
    else if (m.beatsPerBar == 2)
    {
        put (snare, beatSteps, 'x');
    }
    else
    {
        for (int b = 1; b < m.beatsPerBar; b += 2)
            put (snare, b * beatSteps, 'x');
    }

    for (int sIdx = 0; sIdx < steps; sIdx += subSteps)
        put (hat, sIdx, (sIdx % beatSteps == 0) ? 'x' : 'o');

    if (id == "march")
    {
        for (int sIdx = 0; sIdx < steps; sIdx += subSteps)
            put (snare, sIdx, (sIdx % beatSteps == 0) ? 'x' : 'o');
    }

    p.main = { { 36, kick, 0 }, { 38, snare, 0 }, { 42, hat, 0.2 } };
    p.fill = 1;
    p.crash = id != "waltz" && id != "lofi" && id != "jazz" && id != "bossa";
    return p;
}

bool isHat (int note)  { return note == 42 || note == 44 || note == 46 || note == 51; }
bool isKick (int note) { return note == 35 || note == 36; }

Notes generateDrums (Context& ctx, const TrackSpec& t, Rng& rng)
{
    const Meter& m = ctx.meter;
    std::string id = t.pattern == "auto" ? std::string ("pop") : t.pattern;

    const bool fourFour = m.num == 4 && m.den == 4;

    if (id == "waltz" && fourFour)
        id = "pop";

    const DrumPattern pat = fourFour ? fourFourPattern (id) : genericPattern (id, m);
    const int steps = m.stepsPerBar;
    const int beatSteps = m.beatTicks / kStep;

    Notes out;

    auto hit = [&] (int tick, int note, double velMul, int len = kStep)
    {
        RawNote n;
        n.start = tick;
        n.length = len;
        n.pitch = note;
        n.velocity = t.velocity * velMul * ctx.energyAt (tick);
        out.push_back (n);
    };

    const int lastBar = ctx.bars - 1;

    for (int bar = t.entryBar; bar < ctx.partEndBar (t); ++bar)
    {
        const int bs = bar * m.ticksPerBar;

        if (ctx.hasFinalBar() && bar == lastBar)
        {
            hit (bs, (pat.fill == 2 || id == "tribal") ? 35 : 36, 1.15);

            if (pat.crash)
                hit (bs, 49, 1.1, kPpq * 2);

            continue;
        }

        const bool phraseEnd = (bar + 1) % 4 == 0 && bar != lastBar;
        int fillSteps = 0;

        if (pat.fill != 0 && phraseEnd && t.density >= 0.3)
        {
            const bool bigFill = (bar + 1) % 8 == 0 && t.density >= 0.55;
            fillSteps = std::min (bigFill ? beatSteps * 2 : beatSteps, steps / 2);

            if (! bigFill && rng.chance (0.3))
                fillSteps = 0;
        }

        const int fillStart = steps - fillSteps;

        // Main lanes, with the alternate lanes swapped in on odd bars.
        std::vector<Lane> lanes = pat.main;

        if (bar % 2 == 1)
        {
            for (auto& alt : pat.alt)
            {
                lanes.erase (std::remove_if (lanes.begin(), lanes.end(), [&] (const Lane& l) { return l.note == alt.note; }), lanes.end());
                lanes.push_back (alt);
            }
        }

        for (auto& lane : lanes)
        {
            if (t.density < lane.minDensity)
                continue;

            const int n = std::min (steps, (int) lane.steps.size());

            for (int sIdx = 0; sIdx < n; ++sIdx)
            {
                const char c = lane.steps[(size_t) sIdx];

                if (c == '.')
                    continue;

                if (fillSteps > 0 && sIdx >= fillStart && ! (isKick (lane.note) && sIdx == fillStart))
                    continue;

                if (c == 'o' && t.density < 0.3)
                    continue;

                if (isHat (lane.note) && sIdx % 2 == 1 && t.density < 0.45 && id != "jazz")
                    continue;

                const double v = c == 'X' ? 1.15 : (c == 'x' ? 1.0 : 0.55);
                hit (bs + sIdx * kStep, lane.note, v);
            }
        }

        const bool sectionStart = bar > 0 && (bar == t.entryBar || bar % 8 == 0);

        if (pat.crash && (sectionStart || (bar == 0 && ctx.spec.energy > 0.65)))
            hit (bs, 49, id == "ambient" ? 0.6 : 1.05, kPpq * 2);

        if (fillSteps > 0)
        {
            static const int kitToms[] = { 38, 38, 50, 50, 48, 48, 47, 47, 45, 45, 43, 43, 41, 41 };
            static const int lowToms[] = { 45, 45, 43, 43, 41, 41 };

            for (int i = 0; i < fillSteps; ++i)
            {
                const int tick = bs + (fillStart + i) * kStep;
                const double ramp = 0.7 + 0.4 * (double) i / std::max (1, fillSteps - 1);

                if (pat.fill == 1)
                {
                    if (t.density < 0.5 && i % 2 == 1)
                        continue;

                    const int idx = (int) ((double) i / fillSteps * 14);
                    hit (tick, kitToms[std::min (13, idx)], ramp);
                }
                else if (pat.fill == 2)
                {
                    hit (tick, 38, 0.35 + 0.7 * (double) i / fillSteps);

                    if (t.density > 0.7)
                        hit (tick + kStep / 2, 38, 0.3 + 0.7 * (double) i / fillSteps);

                    if (i >= fillSteps - 2)
                        hit (tick, 41, ramp);
                }
                else if (pat.fill == 3)
                {
                    if (i % 2 == 0 || t.density > 0.6)
                        hit (tick, lowToms[std::min (5, i * 6 / fillSteps)], ramp);
                }
            }
        }

        if (id == "trap" && rng.chance (0.5) && fourFour)
        {
            // Hi-hat roll on the last beat: replace the 16ths with 32nds or 16th triplets.
            const int rollStart = bs + 12 * kStep;
            out.erase (std::remove_if (out.begin(), out.end(), [&] (const RawNote& n)
                       { return n.pitch == 42 && n.start >= rollStart && n.start < rollStart + kPpq; }), out.end());

            const int div = rng.chance (0.5) ? 8 : 6;

            for (int i = 0; i < div; ++i)
                hit (rollStart + i * kPpq / div, 42, 0.55 + 0.4 * (double) i / div, kPpq / div);
        }

        if (id == "jazz")
        {
            // Snare "comping" ghost notes on swung off-beats.
            for (int b = 0; b < m.beatsPerBar; ++b)
                if (rng.chance (0.22 + 0.3 * t.density))
                    hit (bs + b * m.beatTicks + m.subdivTicks, 38, rng.uniform (0.35, 0.6));
        }
    }

    // Remove duplicates (same drum, same tick), keeping the loudest.
    std::sort (out.begin(), out.end(), [] (const RawNote& a, const RawNote& b)
               { return a.start != b.start ? a.start < b.start : (a.pitch != b.pitch ? a.pitch < b.pitch : a.velocity > b.velocity); });
    out.erase (std::unique (out.begin(), out.end(), [] (const RawNote& a, const RawNote& b)
               { return a.start == b.start && a.pitch == b.pitch; }), out.end());

    return out;
}

//==============================================================================
/** Bowed, blown and sung parts get CC1 (dynamics in orchestral libraries) and CC11
    (expression) curves that follow the piece's dynamics and swell through each phrase.
*/
void addExpression (const Context& ctx, const TrackSpec& t, const Notes& notes, Controls& cc)
{
    const int p = t.program;
    const bool sustained = (p >= 40 && p <= 79 && p != 45 && p != 46 && p != 47) || (p >= 88 && p <= 95);

    if (! sustained || t.role == Role::Percussion || ! cc.empty() || notes.empty())
        return;

    int first = ctx.totalTicks;

    for (auto& n : notes)
        first = std::min (first, n.start);

    const int step = ctx.meter.beatTicks;
    const int tpb = ctx.meter.ticksPerBar;
    int last = -100;

    for (int tick = (first / step) * step; tick < ctx.totalTicks; tick += step)
    {
        const auto& sec = ctx.sectionAt (tick);
        const double pos = (double) (tick - sec.startBar * tpb) / (double) std::max (1, sec.bars * tpb);
        const double energy = clampv ((ctx.energyAt (tick) - 0.55) / 0.75, 0.0, 1.0);
        double v = 34.0 + 62.0 * energy + 22.0 * std::sin (kPi * pos);
        const double sinceEntry = (double) (tick - first) / tpb;

        if (sinceEntry < 1.0)
            v *= 0.7 + 0.3 * sinceEntry;

        const int value = clampv ((int) std::lround (v), 20, 127);

        if (std::abs (value - last) >= 2)
        {
            cc.push_back ({ tick, 1, value });
            cc.push_back ({ tick, 11, clampv (64 + value / 2, 0, 127) });
            last = value;
        }
    }
}

Track finalizeTrack (const Context& ctx, const TrackSpec& ts, Notes raw, Controls cc, Rng& rng)
{
    Track track;
    track.name = ts.name;
    track.role = ts.role;
    track.program = ts.program;
    track.channel = ts.channel;
    track.isDrum = ts.role == Role::Percussion;

    const auto& s = ctx.spec;
    const bool canSwing = s.swing > 0.0 && ! ctx.meter.compound;
    const int unit = s.swingSixteenths ? kStep : kPpq / 2;

    for (auto& n : raw)
    {
        int start = n.start;

        if (canSwing && start % (2 * unit) == unit)
            start += (int) std::lround (s.swing * unit / 3.0);

        if (s.humanize > 0.0 && start > 0)
            start += (int) std::lround (rng.uniform (-1.0, 1.0) * s.humanize * (track.isDrum ? 6.0 : 10.0));

        start = std::max (0, start + n.micro);

        if (start >= ctx.totalTicks)
            continue;

        int length = std::max (30, n.length);

        if (start + length > ctx.totalTicks)
            length = std::max (30, ctx.totalTicks - start);

        const double vel = n.velocity * (1.0 + rng.uniform (-1.0, 1.0) * s.humanize * 0.07);

        Note note;
        note.start = start;
        note.length = length;
        note.pitch = clampv (n.pitch, 0, 127);
        note.velocity = clampv ((int) std::lround (vel), 1, 127);
        note.articulation = n.articulation;
        track.notes.push_back (note);
    }

    std::sort (track.notes.begin(), track.notes.end(), [] (const Note& a, const Note& b)
               { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });

    // A re-struck pitch cuts the previous one off, and exact duplicates are merged.
    std::vector<Note> cleaned;
    std::map<int, size_t> lastByPitch;

    for (auto& n : track.notes)
    {
        auto it = lastByPitch.find (n.pitch);

        if (it != lastByPitch.end())
        {
            Note& prev = cleaned[it->second];

            if (n.start - prev.start < 12)
            {
                // Two strikes of the same note a few ticks apart (layered parts, humanised
                // timing) are one note.
                prev.velocity = std::max (prev.velocity, n.velocity);
                prev.length = std::max (prev.length, n.start + n.length - prev.start);
                continue;
            }

            if (prev.start + prev.length > n.start)
                prev.length = std::max (10, n.start - prev.start - 1);
        }

        lastByPitch[n.pitch] = cleaned.size();
        cleaned.push_back (n);
    }

    track.notes = std::move (cleaned);

    for (auto& c : cc)
        if (c.tick >= 0 && c.tick < ctx.totalTicks)
            track.controls.push_back (c);

    std::sort (track.controls.begin(), track.controls.end(), [] (const ControlChange& a, const ControlChange& b)
               { return a.tick != b.tick ? a.tick < b.tick : a.controller < b.controller; });

    return track;
}

uint64_t hashString (const std::string& s)
{
    uint64_t h = 1469598103934665603ull;

    for (unsigned char c : s)
    {
        h ^= c;
        h *= 1099511628211ull;
    }

    return h;
}

} // namespace detail

using namespace detail;

const char* articulationName (Articulation a)
{
    switch (a)
    {
        case Articulation::Default:   return "default";
        case Articulation::Legato:    return "legato";
        case Articulation::Sustain:   return "sustain";
        case Articulation::Staccato:  return "staccato";
        case Articulation::Spiccato:  return "spiccato";
        case Articulation::Marcato:   return "marcato";
        case Articulation::Pizzicato: return "pizzicato";
        case Articulation::Tremolo:   return "tremolo";
        case Articulation::Trill:     return "trill";
    }

    return "default";
}

//==============================================================================
std::string Composition::summary() const
{
    std::string chordsText;

    for (size_t i = 0; i < progression.size() && i < 8; ++i)
    {
        if (! chordsText.empty())
            chordsText += " \xE2\x80\x93 ";

        chordsText += progression[i];
    }

    if (progression.size() > 8)
        chordsText += " \xE2\x80\xA6";

    std::string key = keyName (keyRoot, mode);
    const int secs = (int) std::lround (seconds());
    const std::string duration = std::to_string (secs / 60) + ":" + (secs % 60 < 10 ? "0" : "") + std::to_string (secs % 60);

    if (endKeyRoot != keyRoot)
        key += " \xE2\x86\x92 " + pitchClassName (endKeyRoot, keyPrefersFlats (endKeyRoot, mode));

    return key + " \xC2\xB7 " + std::to_string (tempo) + " BPM \xC2\xB7 " + std::to_string (tsNum) + "/"
         + std::to_string (tsDen) + " \xC2\xB7 " + std::to_string (bars) + " bars (" + duration + ")"
         + (chordsText.empty() ? "" : " \xC2\xB7 " + chordsText);
}

int Composition::noteCount() const
{
    int n = 0;

    for (auto& t : tracks)
        n += (int) t.notes.size();

    return n;
}

namespace
{
Composition composeOnce (const StyleSpec& specIn, uint64_t seed)
{
    StyleSpec spec = specIn;
    normalizeSpec (spec);

    Context ctx (spec);
    ctx.bars = spec.bars;
    ctx.totalTicks = spec.bars * ctx.meter.ticksPerBar;
    ctx.tonicPitch = 60 + spec.keyRoot - (spec.keyRoot > 6 ? 12 : 0);

    for (auto& t : spec.tracks)
        ctx.hasBassTrack = ctx.hasBassTrack || t.role == Role::Bass;

    {
        Rng formRng (seed ^ 0x50F0E1ull);
        planSections (ctx, formRng);
    }
    Rng chordRng (seed ^ 0xC0FFEEull);
    buildChords (ctx, chordRng);

    Composition comp;
    comp.title = spec.title;
    comp.tempo = spec.tempo;
    comp.tsNum = spec.tsNum;
    comp.tsDen = spec.tsDen;
    comp.bars = spec.bars;
    comp.ticksPerBar = ctx.meter.ticksPerBar;
    comp.lengthTicks = ctx.totalTicks;
    comp.keyRoot = spec.keyRoot;
    comp.mode = spec.mode;
    comp.progression = spec.progression;
    comp.endKeyRoot = ctx.sections.empty() ? spec.keyRoot : ctx.sections.back().keyRoot;

    // Section markers and the form, as for cues.
    for (size_t i = 0; i < ctx.sections.size(); ++i)
    {
        const auto& sec = ctx.sections[i];
        const std::string label = sec.type == 'I' ? std::string ("Intro") : std::string (1, sec.type);

        if (i > 0 && ctx.sections[i - 1].type == sec.type && sec.type != 'I')
            continue; // A A reads as one A section on a timeline

        comp.markers.push_back ({ sec.startBar * ctx.meter.ticksPerBar, label });

        if (! comp.form.empty())
            comp.form += " \xE2\x80\x93 ";

        comp.form += label;
    }

    comp.tracks.resize (spec.tracks.size());

    const bool flats = keyPrefersFlats (spec.keyRoot, spec.mode);

    for (auto& c : ctx.chords)
        comp.chords.push_back ({ c.start, c.end, chordName (c.chord, flats), c.roman, c.pcs, c.bassPc });

    // Melodies first, so counter-melodies can steer around the lead.
    std::vector<size_t> order;

    for (size_t i = 0; i < spec.tracks.size(); ++i)
        if (spec.tracks[i].role == Role::Melody)
            order.push_back (i);

    for (size_t i = 0; i < spec.tracks.size(); ++i)
        if (spec.tracks[i].role != Role::Melody)
            order.push_back (i);

    bool themeUsed = false;

    for (auto i : order)
    {
        const auto& ts = spec.tracks[i];
        Rng rng (seed * 0x9E3779B97F4A7C15ull + (uint64_t) (i + 1) * 0x5851F42D4C957F2Dull + hashString (ts.name));
        Notes notes;
        Controls cc;

        switch (ts.role)
        {
            case Role::Melody:
                notes = generateMelody (ctx, ts, rng, ! themeUsed);
                themeUsed = true;
                break;
            case Role::CounterMelody: notes = generateCounterMelody (ctx, ts, rng); break;
            case Role::Chords:        generateChords (ctx, ts, rng, notes, cc, false); break;
            case Role::Pad:           generateChords (ctx, ts, rng, notes, cc, true); break;
            case Role::Arpeggio:      notes = generateArpeggio (ctx, ts, rng); break;
            case Role::Bass:          notes = generateBass (ctx, ts, rng); break;
            case Role::Ostinato:      notes = generateOstinato (ctx, ts, rng); break;
            case Role::Percussion:    notes = generateDrums (ctx, ts, rng); break;
        }

        if (ts.role == Role::Melody && ctx.leadMelody.empty())
        {
            ctx.leadMelody = notes;
            std::sort (ctx.leadMelody.begin(), ctx.leadMelody.end(), [] (const RawNote& a, const RawNote& b) { return a.start < b.start; });
        }

        addExpression (ctx, ts, notes, cc);

        Rng post (seed ^ (0xABCDEFull + i * 31));
        comp.tracks[i] = finalizeTrack (ctx, ts, std::move (notes), std::move (cc), post);
    }

    // Parts written into another track (a solo piano's two hands) join it.
    for (int i = (int) spec.tracks.size() - 1; i >= 0; --i)
    {
        const int into = spec.tracks[(size_t) i].mergeInto;

        if (into < 0 || into >= (int) comp.tracks.size() || into == i || i >= (int) comp.tracks.size())
            continue;

        auto& dst = comp.tracks[(size_t) into];
        auto& src = comp.tracks[(size_t) i];
        dst.notes.insert (dst.notes.end(), src.notes.begin(), src.notes.end());
        dst.controls.insert (dst.controls.end(), src.controls.begin(), src.controls.end());
        std::stable_sort (dst.notes.begin(), dst.notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
        std::stable_sort (dst.controls.begin(), dst.controls.end(), [] (const ControlChange& a, const ControlChange& b) { return a.tick < b.tick; });

        // One hand must not hold a key the other strikes again.
        for (size_t a = 0; a < dst.notes.size(); ++a)
            for (size_t b = a + 1; b < dst.notes.size() && dst.notes[b].start < dst.notes[a].start + dst.notes[a].length; ++b)
                if (dst.notes[b].pitch == dst.notes[a].pitch)
                {
                    if (dst.notes[b].start > dst.notes[a].start)
                        dst.notes[a].length = dst.notes[b].start - dst.notes[a].start;
                    else
                        dst.notes[b].length = 0; // same key, same moment: one strike
                }

        dst.notes.erase (std::remove_if (dst.notes.begin(), dst.notes.end(), [] (const Note& n) { return n.length <= 0; }), dst.notes.end());

        if (dst.role != Role::Melody && src.role == Role::Melody)
            dst.role = Role::Melody;

        comp.tracks.erase (comp.tracks.begin() + i);
    }

    return comp;
}

} // namespace

Composition compose (const StyleSpec& spec, uint64_t seed)
{
    // Every generation is checked: several takes are composed, mechanically repaired where
    // possible, scored, and the best one is kept. Deterministic for a given seed.
    const int takes = spec.orchestral ? 4 : 3;
    Composition best;
    double bestScore = -1.0;
    int tried = 0;

    for (int k = 0; k < takes; ++k)
    {
        const uint64_t takeSeed = k == 0 ? seed : seed * 0x9E3779B97F4A7C15ull + (uint64_t) k * 0x632BE59BD9B4E019ull;
        Composition c = spec.orchestral ? composeCue (spec, takeSeed) : composeOnce (spec, takeSeed);
        repairComposition (c);
        c.quality = checkQuality (c);
        ++tried;

        if (c.quality.score > bestScore + 1e-9)
        {
            bestScore = c.quality.score;
            best = std::move (c);
        }

        if (bestScore >= 9.95)
            break;
    }

    best.quality.candidates = tried;
    return best;
}

} // namespace pm
