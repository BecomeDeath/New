#include "Learned.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <memory>

namespace pm::detail
{

namespace
{
    std::string lowerCase (std::string s)
    {
        for (auto& c : s)
            c = (char) std::tolower ((unsigned char) c);

        return s;
    }

    bool contains (const std::string& s, const char* what) { return s.find (what) != std::string::npos; }

    int meterIndex (const Meter& m)
    {
        if (m.num == 4 && m.den == 4) return 0;
        if (m.num == 3 && m.den == 4) return 1;
        if (m.num == 6 && m.den == 8) return 2;
        if (m.num == 2 && m.den == 4) return 3;
        return -1;
    }

    int barSteps (int meter) { return meter == 0 ? 16 : (meter == 3 ? 8 : 12); }

    int popcount (unsigned v)
    {
        int n = 0;

        for (; v != 0; v &= v - 1)
            ++n;

        return n;
    }

    //==========================================================================
    // Melody: order-2 step model per family and major / minor, with back-off.
    constexpr int kSteps = 15; // -7..+7

    struct StepModel
    {
        std::vector<double> c2 = std::vector<double> (kSteps * kSteps * kSteps, 0.0);
        std::vector<double> c1 = std::vector<double> (kSteps * kSteps, 0.0);
        std::vector<double> c0 = std::vector<double> (kSteps, 0.0);
        double total = 0.0;
    };

    const std::vector<StepModel>& stepModels()
    {
        static const std::vector<StepModel> models = []
        {
            std::vector<StepModel> m ((size_t) learned::NumFamilies * 2);

            for (int i = 0; i < learned::numMelodyCounts; ++i)
            {
                const auto& c = learned::melodyCounts[i];
                auto& sm = m[(size_t) (c.family * 2 + c.minor)];
                const int a = c.prev2 + 7, b = c.prev1 + 7, n = c.next + 7;
                const double count = (double) c.count;
                sm.c2[(size_t) ((a * kSteps + b) * kSteps + n)] += count;
                sm.c1[(size_t) (b * kSteps + n)] += count;
                sm.c0[(size_t) n] += count;
                sm.total += count;
            }

            return m;
        }();

        return models;
    }

    double stepProb (const StepModel& sm, int a, int b, int n)
    {
        double p0 = (sm.c0[(size_t) n] + 1.0) / (sm.total + kSteps);

        if (b < 0)
            return p0;

        double row1 = 0.0;

        for (int k = 0; k < kSteps; ++k)
            row1 += sm.c1[(size_t) (b * kSteps + k)];

        const double beta = 5.0;
        const double p1 = (sm.c1[(size_t) (b * kSteps + n)] + beta * p0) / (row1 + beta);

        if (a < 0)
            return p1;

        double row2 = 0.0;

        for (int k = 0; k < kSteps; ++k)
            row2 += sm.c2[(size_t) ((a * kSteps + b) * kSteps + k)];

        const double alpha = 5.0;
        return (sm.c2[(size_t) ((a * kSteps + b) * kSteps + n)] + alpha * p1) / (row2 + alpha);
    }

    //==========================================================================
    // Progressions, merged across families.
    struct ProgEntry
    {
        std::vector<std::string> romans;
        int family = 0;
        bool minor = false;
        double count = 0.0;
    };

    std::vector<std::string> splitWords (const char* text)
    {
        std::vector<std::string> out;
        std::string w;

        for (const char* p = text; *p != 0; ++p)
        {
            if (*p == ' ')
            {
                if (! w.empty()) out.push_back (w);
                w.clear();
            }
            else
            {
                w += *p;
            }
        }

        if (! w.empty())
            out.push_back (w);

        return out;
    }

    const std::vector<ProgEntry>& progressionEntries()
    {
        static const std::vector<ProgEntry> entries = []
        {
            std::vector<ProgEntry> e;

            for (int i = 0; i < learned::numProgressions; ++i)
            {
                const auto& p = learned::progressions[i];
                e.push_back ({ splitWords (p.romans), p.family, p.minor != 0, (double) p.count });
            }

            return e;
        }();

        return entries;
    }

    /** Learned minor-key numerals count natural-minor degrees; harmonic minor raises the 7th. */
    std::string toHarmonicMinor (const std::string& r)
    {
        if (r.rfind ("#VII", 0) == 0 || r.rfind ("#vii", 0) == 0)
            return r.substr (1);

        if (r.rfind ("VII", 0) == 0 || r.rfind ("vii", 0) == 0)
            return "b" + r;

        return r;
    }
}

//==============================================================================
FamilyMix familiesFor (const StyleSpec& s)
{
    using namespace learned;
    FamilyMix f;
    auto& w = f.weight;
    const auto id = lowerCase (s.styleId);

    std::string words;

    for (auto& line : s.interpretation)
        words += lowerCase (line) + " ";

    words += lowerCase (s.title);

    auto add = [&] (Family fam, double v) { w[(size_t) fam] += v; };

    if (contains (id, "villain") || contains (id, "horror") || contains (id, "stealth"))
    {
        add (Dark, 0.8);
        add (Cinematic, 0.5);
    }
    else if (contains (id, "lament"))
    {
        add (Emotional, 1.0);
        add (Cinematic, 0.4);
    }
    else if (contains (id, "pastoral") || contains (id, "whimsical") || contains (id, "celtic") || contains (id, "lullaby"))
    {
        add (Bright, 0.8);
        add (Cinematic, 0.3);

        if (contains (id, "lullaby"))
            add (Emotional, 0.5);
    }
    else if (contains (id, "orchestral") || contains (id, "cinematic") || contains (id, "epic") || contains (id, "heroic")
             || contains (id, "fanfare") || contains (id, "swashbuckler") || contains (id, "space") || contains (id, "mystery")
             || contains (id, "fantasy") || contains (id, "action") || contains (id, "march"))
    {
        add (Cinematic, 1.0);

        if (contains (id, "action") || contains (id, "epic"))         add (Energetic, 0.3);
        if (contains (id, "fanfare") || contains (id, "heroic"))      add (Bright, 0.3);
        if (contains (id, "mystery") || contains (id, "space"))       add (Dark, 0.2);
    }
    else if (contains (id, "jazz") || contains (id, "lofi") || contains (id, "bossa") || contains (id, "rnb") || contains (id, "blues"))
    {
        add (Jazz, 0.7);
        add (Emotional, 0.3);
    }
    else if (contains (id, "piano") || contains (id, "classical") || contains (id, "hymn") || contains (id, "waltz") || contains (id, "ambient"))
    {
        add (Emotional, 0.5);
        add (Cinematic, 0.5);
    }
    else if (! id.empty())
    {
        add (Energetic, 0.5);
        add (All, 0.5);

        if (contains (id, "pop") || contains (id, "reggae") || contains (id, "country") || contains (id, "chiptune"))
            add (Bright, 0.3);
    }
    else if (s.orchestral)
    {
        add (Cinematic, 1.0);
    }

    // Moods in the prompt.
    if (contains (words, "dark") || contains (words, "sinister") || contains (words, "evil") || contains (words, "scary"))    add (Dark, 0.5);
    if (contains (words, "sad") || contains (words, "emotional") || contains (words, "romantic") || contains (words, "tender")) add (Emotional, 0.5);
    if (contains (words, "happy") || contains (words, "cheerful") || contains (words, "uplifting") || contains (words, "playful")) add (Bright, 0.5);
    if (contains (words, "epic") || contains (words, "battle") || contains (words, "energetic") || contains (words, "intense")) add (Energetic, 0.3);

    if (s.energy >= 0.75)                                    add (Energetic, 0.2);
    if (s.energy <= 0.35 && (s.mode == Mode::Minor || s.mode == Mode::Dorian)) add (Emotional, 0.2);

    add (All, 0.15); // a little of everything keeps rare families from running dry

    return f;
}

//==============================================================================
std::vector<RNote> learnedBarRhythm (const Meter& m, double density, MelStyle style, const FamilyMix& mix, bool needDownbeat, Rng& rng)
{
    const int meter = meterIndex (m);

    if (meter < 0 || learned::numRhythmPatterns == 0)
        return {};

    const int steps = barSteps (meter);
    const double scale = steps / 16.0;
    double target;

    switch (style)
    {
        case MelStyle::Sparse:   target = (1.5 + 2.5 * density) * scale; break;
        case MelStyle::Rhythmic: target = (3.0 + 8.0 * density) * scale; break;
        default:                 target = (2.0 + 6.0 * density) * scale; break;
    }

    struct Cand { uint16_t onsets, sounding; double weight; };
    std::map<std::pair<uint16_t, uint16_t>, double> merged;

    for (double tolerance : { 1.2, 2.2, 3.5 })
    {
        merged.clear();

        for (int i = 0; i < learned::numRhythmPatterns; ++i)
        {
            const auto& p = learned::rhythmPatterns[i];
            const double fw = mix.weight[p.family];

            if (p.meter != meter || fw <= 0.0)
                continue;

            const int n = popcount (p.onsets);

            if (n == 0 || std::abs (n - target) > tolerance)
                continue;

            if (needDownbeat && (p.onsets & 1) == 0 && (p.sounding & 1) != 0)
                continue; // would be a tie from the previous bar

            // Notes shorter than a 16th at fast tempi, and long silences in rhythmic lines, are filtered by count.
            merged[{ p.onsets, p.sounding }] += fw * std::pow ((double) p.count, 0.6);
        }

        if (merged.size() >= 6)
            break;
    }

    if (merged.empty())
        return {};

    std::vector<Cand> cands;
    double total = 0.0;

    for (auto& kv : merged)
    {
        cands.push_back ({ kv.first.first, kv.first.second, kv.second });
        total += kv.second;
    }

    double r = rng.uniform (0.0, total);
    const Cand* chosen = &cands.back();

    for (auto& c : cands)
    {
        r -= c.weight;

        if (r <= 0.0)
        {
            chosen = &c;
            break;
        }
    }

    const int stepTicks = m.ticksPerBar / steps;
    std::vector<int> onsets;

    for (int k = 0; k < steps; ++k)
        if ((chosen->onsets >> k) & 1)
            onsets.push_back (k);

    std::vector<RNote> out;

    if (onsets.front() > 0)
        out.push_back ({ 0, onsets.front() * stepTicks, true });

    for (size_t i = 0; i < onsets.size(); ++i)
    {
        const int start = onsets[i];
        const int next = i + 1 < onsets.size() ? onsets[i + 1] : steps;
        int end = start + 1;

        while (end < next && ((chosen->sounding >> end) & 1))
            ++end;

        out.push_back ({ start * stepTicks, (end - start) * stepTicks, false });

        if (end < next)
            out.push_back ({ end * stepTicks, (next - end) * stepTicks, true });
    }

    return out;
}

//==============================================================================
double learnedStepLogProb (const FamilyMix& mix, bool minor, int prev2, int prev1, int next)
{
    const auto& models = stepModels();
    auto clampStep = [] (int s) { return std::max (-7, std::min (7, s)) + 7; };
    const int a = prev2 == kNoIndex ? -1 : clampStep (prev2);
    const int b = prev1 == kNoIndex ? -1 : clampStep (prev1);
    const int n = clampStep (next);

    double p = 0.0, weights = 0.0;

    for (int fam = 0; fam < learned::NumFamilies; ++fam)
    {
        const double w = mix.weight[(size_t) fam];
        const auto& sm = models[(size_t) (fam * 2 + (minor ? 1 : 0))];

        if (w <= 0.0 || sm.total < 500.0)
            continue;

        p += w * stepProb (sm, b < 0 ? -1 : a, b, n);
        weights += w;
    }

    if (weights <= 0.0)
        return std::log (1.0 / kSteps);

    return std::log (std::max (1.0e-6, p / weights));
}

//==============================================================================
std::vector<std::string> learnedProgression (const FamilyMix& mix, Mode mode, Rng& rng, const std::vector<std::vector<std::string>>& avoid)
{
    if (mode != Mode::Major && mode != Mode::Minor && mode != Mode::HarmonicMinor)
        return {};

    const bool minor = mode != Mode::Major;
    std::map<std::vector<std::string>, double> merged;

    for (auto& e : progressionEntries())
    {
        const double w = mix.weight[(size_t) e.family];

        if (e.minor != minor || w <= 0.0)
            continue;

        std::vector<std::string> romans = e.romans;

        if (mode == Mode::HarmonicMinor)
            for (auto& r : romans)
                r = toHarmonicMinor (r);

        bool valid = true;

        for (auto& r : romans)
        {
            Chord c;
            valid = valid && parseChordSymbol (r, 0, mode, c);
        }

        if (! valid || std::find (avoid.begin(), avoid.end(), romans) != avoid.end())
            continue;

        // Flattened by a square root, so the long tail gets a say (variety), not just the top ten.
        merged[romans] += w * std::sqrt (e.count);
    }

    if (merged.empty())
        return {};

    double total = 0.0;

    for (auto& kv : merged)
        total += kv.second;

    double r = rng.uniform (0.0, total);

    for (auto& kv : merged)
    {
        r -= kv.second;

        if (r <= 0.0)
            return kv.first;
    }

    return merged.rbegin()->first;
}

} // namespace pm::detail
