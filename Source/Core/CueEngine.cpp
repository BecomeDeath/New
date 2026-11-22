#include "CueEngine.h"
#include "Learned.h"
#include "ComposerDetail.h"
#include "QualityCheck.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>

namespace pm
{

namespace
{
using namespace detail;

//==============================================================================
// Form

enum class Kind { Intro, Statement, Restatement, Development, Contrast, Build, Climax, Outro, Stinger };

const char* kindLabel (Kind k)
{
    switch (k)
    {
        case Kind::Intro:        return "Intro";
        case Kind::Statement:    return "Theme";
        case Kind::Restatement:  return "Theme (tutti)";
        case Kind::Development:  return "Development";
        case Kind::Contrast:     return "Second Theme";
        case Kind::Build:        return "Build";
        case Kind::Climax:       return "Climax";
        case Kind::Outro:        return "Outro";
        case Kind::Stinger:      return "End Stinger";
    }

    return "";
}

char kindLetter (Kind k)
{
    switch (k)
    {
        case Kind::Intro:        return 'I';
        case Kind::Statement:    return 'A';
        case Kind::Restatement:  return 'A';
        case Kind::Development:  return 'D';
        case Kind::Contrast:     return 'B';
        case Kind::Build:        return 'T';
        case Kind::Climax:       return 'X';
        case Kind::Outro:        return 'O';
        case Kind::Stinger:      return 'S';
    }

    return 'A';
}

struct CueSection
{
    Kind kind = Kind::Statement;
    int startBar = 0;
    int bars = 4;
    int keyRoot = 0;
    Mode mode = Mode::Major;
    double i0 = 0.4, i1 = 0.4;   // intensity at the start and end of the section
    bool hit = false;            // swell into a tutti hit on the downbeat
    bool run = false;            // run / glissando leading into it

    double intensity() const { return 0.5 * (i0 + i1); }
};

bool isMinorish (Mode m)
{
    switch (m)
    {
        case Mode::Minor: case Mode::Dorian: case Mode::Phrygian: case Mode::HarmonicMinor:
        case Mode::MelodicMinor: case Mode::PentatonicMinor: case Mode::Locrian: case Mode::Japanese:
            return true;
        default:
            return false;
    }
}

bool isExotic (Mode m)
{
    return m == Mode::Japanese || m == Mode::DoubleHarmonic || m == Mode::Blues || m == Mode::Locrian;
}

int tonicPitchFor (int keyRoot) { return 60 + keyRoot - (keyRoot > 6 ? 12 : 0); }

int targetBarsFor (const StyleSpec& s)
{
    if (s.length == "bars")
        return std::max (8, std::min (160, s.bars));

    if (s.length == "sketch")
        return 16;

    const double seconds = s.length == "cue" ? 70.0 : (s.length == "extended" ? 225.0 : 150.0);
    const double quarters = s.tsNum * 4.0 / s.tsDen;
    const int bars = (int) std::lround (seconds * s.tempo / 60.0 / quarters / 4.0) * 4;
    return std::max (16, std::min (160, bars));
}

std::vector<CueSection> planForm (const StyleSpec& s, int targetBars, bool loopable, double energy, Rng& rng)
{
    struct P { Kind k; int units; };
    const int N = std::max (4, (targetBars + 2) / 4);
    std::vector<P> plan;

    using K = Kind;
    const int layout = rng.range (0, 3);

    if (N <= 4)
    {
        plan = layout % 2 == 0 ? std::vector<P> { { K::Statement, 2 }, { K::Climax, 1 }, { K::Outro, 1 } }
                               : std::vector<P> { { K::Intro, 1 }, { K::Statement, 2 }, { K::Outro, 1 } };
    }
    else if (N == 5)
    {
        plan = std::vector<P> { { K::Intro, 1 }, { K::Statement, 2 }, { K::Climax, 1 }, { K::Outro, 1 } };
    }
    else if (N <= 7)
    {
        plan = layout % 2 == 0 ? std::vector<P> { { K::Intro, 1 }, { K::Statement, 2 }, { K::Build, 1 }, { K::Climax, 2 }, { K::Outro, 1 } }
                               : std::vector<P> { { K::Intro, 1 }, { K::Statement, 2 }, { K::Development, 1 }, { K::Climax, 2 }, { K::Outro, 1 } };

        if (N == 6)
            plan.erase (plan.begin() + 2);   // no room for a build
    }
    else if (N <= 9)
    {
        switch (layout)
        {
            case 0:  plan = std::vector<P> { { K::Intro, 1 }, { K::Statement, 2 }, { K::Contrast, 2 }, { K::Build, 1 }, { K::Climax, 2 }, { K::Outro, 1 } }; break;
            case 1:  plan = std::vector<P> { { K::Intro, 1 }, { K::Statement, 2 }, { K::Development, 2 }, { K::Build, 1 }, { K::Climax, 2 }, { K::Outro, 1 } }; break;
            case 2:  plan = std::vector<P> { { K::Intro, 1 }, { K::Statement, 2 }, { K::Contrast, 2 }, { K::Climax, 2 }, { K::Outro, 1 } }; break;
            default: plan = std::vector<P> { { K::Intro, 1 }, { K::Statement, 2 }, { K::Restatement, 2 }, { K::Build, 1 }, { K::Climax, 2 }, { K::Outro, 1 } }; break;
        }
    }
    else if (N <= 12)
    {
        switch (layout)
        {
            case 0:  plan = std::vector<P> { { K::Intro, 1 }, { K::Statement, 2 }, { K::Restatement, 2 }, { K::Contrast, 2 }, { K::Build, 1 }, { K::Climax, 2 }, { K::Outro, 1 } }; break;
            case 1:  plan = std::vector<P> { { K::Intro, 1 }, { K::Statement, 2 }, { K::Contrast, 2 }, { K::Restatement, 2 }, { K::Build, 1 }, { K::Climax, 2 }, { K::Outro, 1 } }; break;
            case 2:  plan = std::vector<P> { { K::Intro, 1 }, { K::Statement, 2 }, { K::Development, 2 }, { K::Contrast, 2 }, { K::Build, 1 }, { K::Climax, 2 }, { K::Outro, 1 } }; break;
            default: plan = std::vector<P> { { K::Intro, 1 }, { K::Statement, 2 }, { K::Restatement, 2 }, { K::Development, 2 }, { K::Build, 1 }, { K::Climax, 2 }, { K::Outro, 1 } }; break;
        }
    }
    else
    {
        switch (layout)
        {
            case 0:  plan = std::vector<P> { { K::Intro, 1 }, { K::Statement, 2 }, { K::Restatement, 2 }, { K::Development, 2 }, { K::Contrast, 2 }, { K::Build, 1 }, { K::Climax, 2 }, { K::Outro, 1 } }; break;
            case 1:  plan = std::vector<P> { { K::Intro, 1 }, { K::Statement, 2 }, { K::Contrast, 2 }, { K::Restatement, 2 }, { K::Development, 2 }, { K::Build, 1 }, { K::Climax, 2 }, { K::Outro, 1 } }; break;
            case 2:  plan = std::vector<P> { { K::Intro, 1 }, { K::Statement, 2 }, { K::Development, 2 }, { K::Contrast, 2 }, { K::Restatement, 2 }, { K::Build, 1 }, { K::Climax, 2 }, { K::Outro, 1 } }; break;
            default: plan = std::vector<P> { { K::Intro, 1 }, { K::Statement, 2 }, { K::Restatement, 2 }, { K::Contrast, 2 }, { K::Development, 2 }, { K::Build, 1 }, { K::Climax, 2 }, { K::Outro, 1 } }; break;
        }
    }

    // Energetic cues sometimes open cold, straight into the theme.
    if (energy >= 0.7 && N >= 8 && plan.front().k == K::Intro && rng.chance (0.35))
        plan.erase (plan.begin());

    if (N >= 20)
    {
        // A second journey: develop again, contrast again, build to a bigger final climax.
        plan.insert (plan.end() - 1, { { Kind::Development, 2 }, { Kind::Contrast, 2 }, { Kind::Build, 1 }, { Kind::Climax, 2 } });
    }

    auto total = [&] { int t = 0; for (auto& p : plan) t += p.units; return t; };

    auto lastOf = [&] (Kind k) -> P*
    {
        for (auto it = plan.rbegin(); it != plan.rend(); ++it)
            if (it->k == k)
                return &*it;

        return nullptr;
    };

    auto firstOf = [&] (Kind k) -> P*
    {
        for (auto& p : plan)
            if (p.k == k)
                return &p;

        return nullptr;
    };

    // Grow towards the target, giving extra time where a composer would.
    for (int guard = 0; total() < N && guard < 64; ++guard)
    {
        P* candidates[] = { lastOf (Kind::Climax), firstOf (Kind::Intro), firstOf (Kind::Development),
                            firstOf (Kind::Contrast), lastOf (Kind::Development), lastOf (Kind::Contrast), lastOf (Kind::Outro) };
        const int caps[] = { 4, 2, 3, 3, 3, 3, 2 };
        bool grew = false;

        for (int c = 0; c < 7 && ! grew; ++c)
        {
            if (candidates[c] != nullptr && candidates[c]->units < caps[c])
            {
                ++candidates[c]->units;
                grew = true;
            }
        }

        if (! grew)
        {
            if (auto* x = lastOf (Kind::Climax))
                ++x->units;
            else
                break;
        }
    }

    std::vector<CueSection> out;
    const Mode home = s.mode;
    const int key = s.keyRoot;

    // Contrast key: the relative major/minor (or the subdominant), unless the mode's colour
    // is the point (exotic scales stay put).
    int contrastKey = key;
    Mode contrastMode = home;

    if (! isExotic (home) && home != Mode::Phrygian)
    {
        if (isMinorish (home))
        {
            contrastKey = wrapPc (key + 3);
            contrastMode = Mode::Major;
        }
        else if (rng.chance (0.6))
        {
            contrastKey = wrapPc (key + 9);
            contrastMode = Mode::Minor;
        }
        else
        {
            contrastKey = wrapPc (key + 5);
            contrastMode = home == Mode::Lydian || home == Mode::Mixolydian ? Mode::Major : home;
        }
    }

    const int climaxKey = wrapPc (key + s.finalLift);
    int climaxCount = 0;

    for (auto& p : plan)
        climaxCount += p.k == Kind::Climax ? 1 : 0;

    const double scale = 0.42 + 0.58 * energy;
    auto I = [&] (double v) { return std::min (1.0, 0.08 + v * scale); };

    int bar = 0, climaxesSeen = 0, contrastsSeen = 0, developmentsSeen = 0;

    for (size_t i = 0; i < plan.size(); ++i)
    {
        CueSection sec;
        sec.kind = plan[i].k;
        sec.startBar = bar;
        sec.bars = plan[i].units * 4;
        sec.keyRoot = key;
        sec.mode = home;

        switch (sec.kind)
        {
            case Kind::Intro:        sec.i0 = I (0.18); sec.i1 = I (0.3); break;
            case Kind::Statement:    sec.i0 = I (0.36); sec.i1 = I (0.44); break;
            case Kind::Restatement:  sec.i0 = I (0.56); sec.i1 = I (0.64); sec.run = true; sec.hit = energy >= 0.7; break;
            case Kind::Development:  sec.i0 = I (developmentsSeen == 0 ? 0.55 : 0.62); sec.i1 = I (developmentsSeen == 0 ? 0.76 : 0.82); ++developmentsSeen; break;
            case Kind::Contrast:
                sec.i0 = I (contrastsSeen == 0 ? 0.34 : 0.42);
                sec.i1 = I (contrastsSeen == 0 ? 0.44 : 0.5);
                sec.keyRoot = contrastKey;
                sec.mode = contrastMode;
                sec.run = true;
                ++contrastsSeen;
                break;
            case Kind::Build:        sec.i0 = I (0.5); sec.i1 = I (0.95); break;
            case Kind::Climax:
                ++climaxesSeen;
                sec.i0 = I (0.96);
                sec.i1 = I (1.0);
                sec.keyRoot = climaxesSeen == climaxCount ? climaxKey : key;
                sec.hit = energy >= 0.5;
                sec.run = true;
                break;
            case Kind::Outro:        sec.i0 = I (0.5); sec.i1 = I (0.1); sec.keyRoot = climaxKey; break;
            case Kind::Stinger:      sec.i0 = I (0.9); sec.i1 = I (0.86); sec.keyRoot = climaxKey; break;
        }

        out.push_back (sec);
        bar += sec.bars;
    }

    // Builds lead into the key of what follows.
    for (size_t i = 0; i + 1 < out.size(); ++i)
        if (out[i].kind == Kind::Build)
        {
            out[i].keyRoot = out[i + 1].keyRoot;
            out[i].mode = out[i + 1].mode;
        }

    const bool button = ! loopable && energy >= 0.65 && rng.chance (0.4);

    if ((loopable || button) && ! out.empty() && out.back().kind == Kind::Outro)
    {
        // A loopable cue ends its loop after the last climax; a short stinger follows for the
        // game to play when the scene ends.
        out.back().kind = Kind::Stinger;
        out.back().bars = 2;
        out.back().i0 = I (0.9);
        out.back().i1 = I (0.86);
    }

    return out;
}

//==============================================================================
// Harmonic language

struct Language
{
    std::vector<std::string> base, authentic, half, epic, sequence, pedalBuild, outro;
    std::string tonic, dominant;
    bool canReharm = false;
    bool majorish = true;
};

Language languageFor (Mode mode, const std::vector<std::string>& base, Rng& rng)
{
    Language L;
    L.base = base;
    auto pick = [&] (std::initializer_list<std::vector<std::string>> options)
    {
        std::vector<std::vector<std::string>> v (options);
        return v[(size_t) rng.range (0, (int) v.size() - 1)];
    };

    switch (mode)
    {
        case Mode::Major:
        case Mode::PentatonicMajor:
            L.tonic = "I"; L.dominant = "V"; L.canReharm = true;
            L.authentic = pick ({ { "IV", "V", "I" }, { "ii", "V", "I" }, { "ii7", "V7", "I" } });
            L.half = pick ({ { "IV", "V" }, { "ii", "V" } });
            L.epic = { "bVI", "bVII", "I" };
            L.sequence = { "I", "IV", "viio", "iii", "vi", "ii", "V", "V" };
            L.pedalBuild = { "IV", "ii", "IV", "V" };
            L.outro = { "I", "IV", "iv", "I" };
            break;

        case Mode::Minor:
        case Mode::MelodicMinor:
        case Mode::PentatonicMinor:
            L.tonic = "i"; L.dominant = "V"; L.canReharm = mode == Mode::Minor; L.majorish = false;
            L.authentic = pick ({ { "iv", "V", "i" }, { "VI", "V", "i" }, { "iio", "V", "i" } });
            L.half = pick ({ { "iv", "V" }, { "VI", "V" } });
            L.epic = { "VI", "VII", "i" };
            L.sequence = { "i", "iv", "VII", "III", "VI", "iio", "V", "V" };
            L.pedalBuild = { "iv", "VI", "iv", "V" };
            L.outro = { "i", "iv", "VI", "i" };
            break;

        case Mode::HarmonicMinor:
            L.tonic = "i"; L.dominant = "V"; L.majorish = false;
            L.authentic = pick ({ { "iv", "V", "i" }, { "iio", "V", "i" } });
            L.half = { "VI", "V" };
            L.epic = { "VI", "V", "i" };
            L.sequence = { "i", "iv", "bVII", "III", "VI", "iio", "V", "V" };
            L.pedalBuild = { "iv", "VI", "iv", "V" };
            L.outro = { "i", "iv", "i", "i" };
            break;

        case Mode::Dorian:
            L.tonic = "i"; L.dominant = "V"; L.majorish = false;
            L.authentic = { "IV", "VII", "i" };
            L.half = { "IV", "V" };
            L.epic = { "VII", "IV", "i" };
            L.sequence = { "i", "IV", "VII", "III", "i", "IV", "V", "V" };
            L.pedalBuild = { "IV", "VII", "IV", "V" };
            L.outro = { "i", "IV", "i", "i" };
            break;

        case Mode::Phrygian:
            L.tonic = "i"; L.dominant = "II"; L.majorish = false;
            L.authentic = { "iv", "II", "i" };
            L.half = { "VI", "II" };
            L.epic = { "VI", "II", "i" };
            L.sequence = { "i", "II", "III", "II", "i", "iv", "VI", "II" };
            L.pedalBuild = { "iv", "II", "iv", "II" };
            L.outro = { "i", "II", "i", "i" };
            break;

        case Mode::Lydian:
            L.tonic = "I"; L.dominant = "V";
            L.authentic = { "II", "V", "I" };
            L.half = { "II", "V" };
            L.epic = { "vi", "II", "I" };
            L.sequence = { "I", "II", "V", "I", "vi", "II", "V", "V" };
            L.pedalBuild = { "II", "vi", "II", "V" };
            L.outro = { "I", "II", "I", "I" };
            break;

        case Mode::Mixolydian:
            L.tonic = "I"; L.dominant = "VII";
            L.authentic = { "IV", "VII", "I" };
            L.half = { "IV", "v" };
            L.epic = { "VII", "IV", "I" };
            L.sequence = { "I", "IV", "VII", "IV", "I", "v", "VII", "VII" };
            L.pedalBuild = { "IV", "VII", "IV", "v" };
            L.outro = { "I", "IV", "I", "I" };
            break;

        default:
        {
            // Exotic scales keep their own progression throughout.
            L.majorish = ! isMinorish (mode);
            L.tonic = base.empty() ? std::string ("I") : base.front();
            L.dominant = base.size() > 1 ? base[1] : L.tonic;
            const std::string pre = base.size() > 2 ? base[base.size() - 2] : L.dominant;
            L.authentic = { pre, L.dominant, L.tonic };
            L.half = { pre, L.dominant };
            L.epic = L.authentic;
            L.sequence = base;
            L.pedalBuild = base;
            L.outro = base;
            break;
        }
    }

    if (L.base.empty())
        L.base = { L.tonic, L.dominant };

    return L;
}

struct BarPlan
{
    std::vector<std::string> chords; // one or two chords in the bar
    int pedal = 0;                   // 0 none, 1 tonic pedal, 2 dominant pedal
};

using Harmony = std::vector<BarPlan>;

bool containsDominant (const std::vector<std::string>& prog, const std::string& dominant)
{
    for (auto& c : prog)
        if (c.rfind (dominant, 0) == 0 && (c.size() == dominant.size() || std::isdigit ((unsigned char) c[dominant.size()]) || c[dominant.size()] == 's'))
            return true;

    return false;
}

/** An 8-bar period (antecedent ending on a half cadence, consequent on an authentic one),
    repeated or shortened to fill `bars`.
*/
Harmony periodHarmony (const Language& L, int bars, bool endOnHalf, bool epicCadence, double complexity, Rng& rng)
{
    Harmony h;
    const auto& b = L.base;
    auto B = [&] (size_t i) { return b[i % b.size()]; };
    const auto& cad = epicCadence ? L.epic : L.authentic;
    const bool modalLoop = ! containsDominant (b, L.dominant);
    const std::string halfEnd = modalLoop ? B (3) : L.half.back();

    auto cadenceBars = [&] (bool half)
    {
        Harmony c;

        if (half)
        {
            c.push_back ({ { L.half[0] } });
            c.push_back ({ { L.half.back() } });
            return c;
        }

        std::vector<std::string> first { cad[0] };

        if (cad.size() >= 3)
            first.push_back (cad[1]);

        // A secondary dominant pushing into the dominant (V7/V -> V -> I).
        if (cad.size() >= 3 && L.canReharm && cad[1] == L.dominant && rng.chance (0.25 + 0.45 * complexity))
            first = { "V7/V", cad[1] };

        c.push_back ({ first });
        c.push_back ({ { cad.back() } });
        return c;
    };

    int made = 0;

    while (made < bars)
    {
        const int remaining = bars - made;
        const bool lastUnit = remaining <= 8;

        if (remaining >= 8)
        {
            h.push_back ({ { B (0) } });
            h.push_back ({ { B (1) } });
            h.push_back ({ { B (2) } });
            h.push_back ({ { halfEnd } });
            h.push_back ({ { B (0) } });
            h.push_back ({ { B (1) } });

            for (auto& c : cadenceBars (endOnHalf && lastUnit))
                h.push_back (c);

            made += 8;
        }
        else if (remaining >= 4)
        {
            h.push_back ({ { B (0) } });
            h.push_back ({ { B (1) } });

            for (auto& c : cadenceBars (endOnHalf && remaining < 8))
                h.push_back (c);

            made += 4;
        }
        else
        {
            for (int i = 0; i < remaining; ++i)
                h.push_back ({ { i == remaining - 1 ? cad.back() : B ((size_t) i) } });

            made += remaining;
        }
    }

    h.resize ((size_t) bars);
    return h;
}

Harmony sectionHarmony (const CueSection& sec, const Language& L, const Language& contrastL, double energy, double complexity,
                        bool drive, Rng& rng)
{
    const int n = sec.bars;
    Harmony h;

    switch (sec.kind)
    {
        case Kind::Intro:
        {
            for (int b = 0; b < n; ++b)
            {
                if (b == n - 1 && n > 1)
                    h.push_back ({ { L.half.back() }, 0 });
                else
                    h.push_back ({ { L.base[(size_t) ((b / 2) % 2) % L.base.size()] }, 1 });
            }

            break;
        }

        case Kind::Statement:
        case Kind::Restatement:
            h = periodHarmony (L, n, false, false, complexity, rng);
            break;

        case Kind::Climax:
            h = periodHarmony (L, n, false, energy >= 0.65 || drive, complexity, rng);
            break;

        case Kind::Contrast:
            h = periodHarmony (contrastL, n, true, false, complexity, rng);
            break;

        case Kind::Development:
        {
            // Descending-fifths sequence, ending on the dominant (a half cadence that the next
            // section answers).
            const auto& seq = L.sequence;
            const int len = (int) seq.size();

            for (int b = 0; b < n; ++b)
            {
                const int fromEnd = n - 1 - b;
                const int idx = ((len - 1 - fromEnd) % len + len) % len;
                h.push_back ({ { seq[(size_t) idx] } });
            }

            break;
        }

        case Kind::Build:
        {
            for (int b = 0; b < n; ++b)
            {
                if (b == n - 1 && L.dominant == "V")
                    h.push_back ({ { "Vsus4", "V" }, 2 });
                else
                    h.push_back ({ { L.pedalBuild[(size_t) b % L.pedalBuild.size()] }, 2 });
            }

            break;
        }

        case Kind::Outro:
        {
            for (int b = 0; b < n; ++b)
            {
                const bool last = b == n - 1;
                std::string c = last ? L.tonic : L.outro[(size_t) b % L.outro.size()];

                // Minor-key endings sometimes brighten to a major tonic (a Picardy third).
                if (last && ! L.majorish && L.canReharm && rng.chance (0.3))
                    c = "I";

                h.push_back ({ { c }, last ? 0 : 1 });
            }

            break;
        }

        case Kind::Stinger:
        {
            h.push_back ({ { L.epic[0], L.epic.size() > 2 ? L.epic[1] : L.dominant } });

            for (int b = 1; b < n; ++b)
                h.push_back ({ { L.tonic } });

            break;
        }
    }

    h.resize ((size_t) n, BarPlan { { L.tonic }, 0 });
    return h;
}

//==============================================================================
// Theme material (scale-index space, relative to the tonic)

struct TNote
{
    int onset = 0;   // ticks from the start of the section
    int dur = 0;
    int idx = 0;     // scale index (0 = tonic)
    bool rest = false;
};

using Line = std::vector<TNote>;

int lastSoundingIndex (const std::vector<RNote>& r)
{
    int last = -1;

    for (int k = 0; k < (int) r.size(); ++k)
        if (! r[(size_t) k].rest)
            last = k;

    return last;
}

/** Composes an 8-bar melodic period (or a 4-bar phrase) over the harmony already in ctx.

    Like a composer's sketch: first a skeleton - one structural note per bar following a
    contour (arch, climb, fifth-and-sigh, heroic push to the octave), each landing on a chord
    tone - then the diminution: the rhythm is filled with directed stepwise motion, passing and
    neighbour tones towards the next structural note. Bar 1 is the motif (with a signature leap),
    bar 2 its sequence, bars 5-6 restate bars 1-2, bar 4 is a half cadence and bar 8 closes.
*/
Line composePeriod (Context& ctx, int sectionStart, int bars, const ScaleMap& sm, int keyRoot, int lo, int hi,
                    MelStyle style, double density, double energy, int leapDirection, bool endOnTonic, Rng& rng)
{
    const Meter& m = ctx.meter;
    const int tpb = m.ticksPerBar;
    const int n = sm.size();
    const int half = bars >= 8 ? bars / 2 : bars;

    static const int shapes[][8] = {
        { 0, 2, 4, 1,  0, 3, 6, 0 },   // arch
        { 0, 4, 3, 1,  2, 5, 7, 0 },   // climbing to a late peak
        { 4, 2, 3, 1,  4, 2, 6, 0 },   // from the fifth, sighing down, then soaring
        { 0, 4, 5, 4,  0, 4, 7, 0 },   // heroic: a fifth, then the push to the octave
        { 2, 0, 3, 1,  2, 4, 5, 0 },   // gentle, circling the third
    };

    // The phrase's contour: one of the classic shapes, or (more often) a new one drawn within
    // the rules of a period - a start on a tonic-chord note, an open half cadence at bar 4,
    // the opening restated at bar 5, the high point in bar 7, home at the end.
    int contour[8];

    if (rng.chance (0.3))
    {
        int shape;

        if (leapDirection < 0)        shape = rng.chance (0.5) ? 2 : 4;
        else if (energy >= 0.65)      shape = rng.chance (0.6) ? 3 : 1;
        else                          shape = rng.pick (std::vector<int> { 0, 1, 2, 4 });

        for (int i = 0; i < 8; ++i)
            contour[i] = shapes[shape][i];
    }
    else
    {
        static const std::vector<int> startsUp { 0, 0, 2, 4, 4 };
        static const std::vector<int> startsDown { 4, 5, 7, 2 };
        contour[0] = rng.pick (leapDirection < 0 ? startsDown : startsUp);
        contour[1] = contour[0] + rng.pick (std::vector<int> { -2, -1, 1, 2, 3, 3 });
        contour[2] = rng.range (1, 6);

        if (contour[2] == contour[1])
            contour[2] += contour[1] > 3 ? -2 : 2;

        contour[3] = rng.pick (std::vector<int> { 1, 4, 3, 1 });
        contour[4] = contour[0];
        contour[5] = contour[1] + (rng.chance (0.4) ? 1 : 0);
        const int highest = std::max ({ contour[0], contour[1], contour[2], contour[5] });
        contour[6] = std::min (energy >= 0.65 ? 9 : 8, highest + rng.range (1, 3));
        contour[7] = 0;
    }

    const bool learnedSteps = n == 7;
    const bool minorScale = n == 7 && sm.intervals[2] == 3;
    int lastStep = kNoIndex, lastStep2 = kNoIndex;

    // The phrase's home tonic sits low in the register, so the contour has room to rise.
    int base = n * (int) std::lround ((lo + (hi - lo) * 0.25) / (double) n);

    while (base + 7 > hi && base - n >= lo) base -= n;
    while (base < lo) base += n;

    auto nearestChordTone = [&] (int target, const ChordEvent& ch)
    {
        for (int d : { 0, 1, -1, 2, -2, 3, -3 })
        {
            const int idx = target + d;

            if (idx >= lo && idx <= hi && indexIsChordTone (sm, idx, ch))
                return idx;
        }

        return std::max (lo, std::min (hi, target));
    };

    std::vector<int> anchors ((size_t) bars + 1);

    for (int b = 0; b < bars; ++b)
    {
        const int shapeBar = bars >= 8 ? b % 8 : 4 + (b % 4);
        const auto& ch = ctx.chordAt (sectionStart + b * tpb + 1);
        int target = base + contour[shapeBar];

        if (b == bars - 1 && endOnTonic)
            target = base;

        anchors[(size_t) b] = nearestChordTone (target, ch);

        if (b == bars - 1 && endOnTonic)
            anchors[(size_t) b] = cadenceIndex (sm, target, lo, hi, ch, CadencePref::Tonic, keyRoot);
    }

    anchors[(size_t) bars] = anchors[(size_t) bars - 1];

    std::vector<std::vector<RNote>> rhythms ((size_t) bars);
    std::vector<std::vector<int>> indices ((size_t) bars);
    Line out;
    int prev = anchors[0];
    int prevPrev = INT_MIN;
    bool leapDone = false;

    for (int b = 0; b < bars; ++b)
    {
        const bool phraseEnd = b == half - 1 || b == bars - 1;
        const bool finalBar = b == bars - 1;
        const bool repeatBar = bars >= 8 && b >= half && b < half + 2;
        const bool sequenceBar = b == 1 && rng.chance (0.55);

        if (repeatBar)
            rhythms[(size_t) b] = rhythms[(size_t) (b - half)];
        else if (sequenceBar)
            rhythms[(size_t) b] = rhythms[0];
        else if (phraseEnd && bars > 1)
            rhythms[(size_t) b] = cadenceRhythm (m, finalBar && endOnTonic, ! finalBar, density, style, rng);
        else
        {
            // Mostly rhythms learned from the corpus; sometimes the hand-written cells.
            if (rng.chance (0.75))
                rhythms[(size_t) b] = learnedBarRhythm (m, density, style, ctx.families, b == 0 || b == half, rng);

            if (rhythms[(size_t) b].empty())
                rhythms[(size_t) b] = fillRhythm (m, 0, tpb, density, style, rng, b > 0);
        }

        const auto& rhythm = rhythms[(size_t) b];
        const int lastK = lastSoundingIndex (rhythm);
        bool firstNote = true;

        // In a cadence bar, the notes after the cadence note are pickups into the next phrase.
        int cadenceK = -1;

        if (phraseEnd && ! finalBar)
            for (int k = 0; k < (int) rhythm.size(); ++k)
                if (! rhythm[(size_t) k].rest && rhythm[(size_t) k].dur >= m.beatTicks)
                    cadenceK = k;

        int pickupsLeft = 0;

        if (cadenceK >= 0)
            for (int k = cadenceK + 1; k < (int) rhythm.size(); ++k)
                pickupsLeft += rhythm[(size_t) k].rest ? 0 : 1;

        for (int k = 0; k < (int) rhythm.size(); ++k)
        {
            const auto& r = rhythm[(size_t) k];

            if (r.rest)
            {
                indices[(size_t) b].push_back (INT_MIN);
                continue;
            }

            const int tick = sectionStart + b * tpb + r.offset;
            const auto& ch = ctx.chordAt (tick);
            const bool strong = isStrongPosition (m, r.offset, r.dur) || tick == ch.start;
            const bool onBeat = r.offset % m.beatTicks == 0;
            int idx;

            const auto& model = (repeatBar || sequenceBar) ? indices[(size_t) (repeatBar ? b - half : 0)] : indices[(size_t) b];
            const bool fromModel = (repeatBar || sequenceBar) && k < (int) model.size() && model[(size_t) k] != INT_MIN;

            if (fromModel)
            {
                // Restatement or sequence of the motif: same shape, moved to this bar's structural note.
                const int modelAnchor = anchors[(size_t) (repeatBar ? b - half : 0)];
                idx = model[(size_t) k] + (anchors[(size_t) b] - modelAnchor);

                if ((strong || onBeat) && ! indexIsChordTone (sm, idx, ch))
                    idx = snapToChordTone (sm, idx, lo, hi, ch);
            }
            else if (firstNote)
            {
                idx = anchors[(size_t) b];
            }
            else if (cadenceK >= 0 && k > cadenceK)
            {
                // Walk up (or down) by step into the next phrase's first note.
                const int next = anchors[(size_t) b + 1];
                const int dir = next >= prev ? 1 : -1;
                idx = next - dir * pickupsLeft;
                --pickupsLeft;
            }
            else if (! leapDone && b == 0 && strong)
            {
                // The motif's signature leap.
                static const int lyrical[] = { 2, 3, 4 };
                const int leap = energy >= 0.6 ? rng.range (3, 4) : lyrical[rng.range (0, 2)];
                idx = nearestChordTone (prev + leapDirection * leap, ch);
                leapDone = true;
            }
            else
            {
                // Directed motion towards the next structural note, spread over the notes left
                // in the bar so the line moves by step when it can.
                int notesLeft = 1;

                for (int j = k + 1; j < (int) rhythm.size(); ++j)
                    notesLeft += rhythm[(size_t) j].rest ? 0 : 1;

                const double ideal = prev + (double) (anchors[(size_t) b + 1] - prev) / notesLeft;
                double bestScore = 1e9;
                idx = prev;

                for (int cand = std::max (lo, prev - 4); cand <= std::min (hi, prev + 4); ++cand)
                {
                    const int step = std::abs (cand - prev);
                    static const double stepCost[] = { 1.3, 0.0, 0.35, 0.9, 1.6 };
                    double sc = std::abs (cand - ideal) * 0.8 + (step == 0 && style == MelStyle::Rhythmic ? 0.6 : stepCost[std::min (step, 4)]);

                    // No trills: don't step straight back to where the line just came from.
                    if (cand == prevPrev && cand != prev && std::abs (prev - prevPrev) <= 1)
                        sc += 1.2;
                    const bool ct = indexIsChordTone (sm, cand, ch);

                    // Strong beats sit on chord tones; weak beats are free for passing tones.
                    if (strong)       sc += ct ? -1.0 : 2.5;
                    else if (onBeat)  sc += ct ? -0.2 : 0.3;

                    if (std::abs (sm.pitch (cand) - sm.pitch (prev)) == 6)
                        sc += 2.0;

                    // How melodies in the corpus move after the last two intervals.
                    if (learnedSteps)
                        sc -= 0.3 * (learnedStepLogProb (ctx.families, minorScale, lastStep2, lastStep, cand - prev) - std::log (1.0 / 15.0));

                    sc += rng.uniform (0.0, 0.35);

                    if (sc < bestScore)
                    {
                        bestScore = sc;
                        idx = cand;
                    }
                }
            }

            if (phraseEnd && k == lastK && ! fromModel)
            {
                const auto pref = finalBar ? (endOnTonic ? CadencePref::Tonic : CadencePref::Open) : CadencePref::Open;
                idx = cadenceIndex (sm, prev, lo, hi, ch, pref, keyRoot);
            }

            idx = std::max (lo, std::min (hi, idx));
            indices[(size_t) b].push_back (idx);
            out.push_back ({ b * tpb + r.offset, r.dur, idx, false });

            if (idx != prev)
                prevPrev = prev;

            lastStep2 = lastStep;
            lastStep = idx - prev;
            prev = idx;
            firstNote = false;
        }
    }

    return out;
}

/** Moves notes that land on the beat onto chord tones (keeping their direction), and keeps the
    line inside [lo, hi]. Used after transforming material (sequence, inversion, augmentation).
*/
void fitLine (Line& line, int sectionStart, const Context& ctx, const ScaleMap& sm, int lo, int hi)
{
    const int n = sm.size();

    for (auto& t : line)
    {
        if (t.rest)
            continue;

        while (t.idx > hi) t.idx -= n;
        while (t.idx < lo) t.idx += n;

        const int tick = sectionStart + t.onset;
        const auto& ch = ctx.chordAt (tick);
        const bool onBeat = t.onset % ctx.meter.beatTicks == 0 || t.dur >= ctx.meter.beatTicks;

        if (onBeat && ! indexIsChordTone (sm, t.idx, ch))
            t.idx = snapToChordTone (sm, t.idx, lo, hi, ch);
    }
}

/** Strong-beat consonance: a melody note that sounds on a strong beat (or holds through a chord
    change) against a chord it rubs with by a semitone is resolved to the nearest chord tone,
    preferring the step down. Held notes are split at the chord change, so a suspension
    resolves (Ab over iio, then G over V) instead of sounding through.
*/
void resolveLine (Line& line, int sectionStart, const Context& ctx, const ScaleMap& sm, int lo, int hi)
{
    const Meter& m = ctx.meter;
    Line out;

    auto harsh = [&] (int pc, const ChordEvent& ch)
    {
        auto pcs = ch.pcs.empty() ? ch.chord.pitchClasses() : ch.pcs;

        if (ch.bassPc >= 0)
            pcs.push_back (ch.bassPc);

        if (std::find (pcs.begin(), pcs.end(), pc) != pcs.end())
            return false;

        for (int c : pcs)
            if (wrapPc (pc - c) == 1 || wrapPc (c - pc) == 1)
                return true;

        return false;
    };

    auto resolve = [&] (int idx, const ChordEvent& ch)
    {
        for (int d : { -1, 1, -2, 2 })
            if (idx + d >= lo && idx + d <= hi && ! harsh (wrapPc (sm.pitch (idx + d)), ch)
                && ch.chord.containsPc (sm.pitch (idx + d)))
                return idx + d;

        return idx;
    };

    for (const auto& t : line)
    {
        if (t.rest || t.dur <= 0)
        {
            out.push_back (t);
            continue;
        }

        const int end = t.onset + t.dur;
        int from = t.onset;
        bool firstPart = true;

        while (from < end)
        {
            const int absFrom = sectionStart + from;
            const auto& ch = ctx.chordAt (absFrom);
            const int change = std::min (end, ch.end - sectionStart);
            TNote part = t;
            part.onset = from;
            part.dur = std::max (1, change - from);

            const int off = from % m.ticksPerBar;
            const bool strong = isStrongPosition (m, off, part.dur) || from % m.beatTicks == 0
                             || (! firstPart && part.dur >= m.beatTicks / 2);

            if (strong && part.dur >= kStep * 2 && harsh (wrapPc (sm.pitch (part.idx)), ch))
                part.idx = resolve (part.idx, ch);

            // A held note whose next part needs no change stays one note.
            if (! firstPart && ! out.empty() && out.back().idx == part.idx && ! out.back().rest
                && out.back().onset + out.back().dur == part.onset)
                out.back().dur += part.dur;
            else if (! firstPart && part.dur < kStep * 2)
                out.back().dur += part.dur; // too short to re-strike: let the previous part hold
            else
                out.push_back (part);

            firstPart = false;
            from = change;
        }
    }

    line = out;
}

/** The theme's motif: its first four to six notes from wherever the tune starts (after any
    opening rest, across the barline if need be), at most two bars, keeping the first note's
    position in its bar. This is the material a composer fragments and sequences.
*/
Line motifOf (const Line& theme, int tpb)
{
    Line m;
    size_t first = 0;

    while (first < theme.size() && theme[first].rest)
        ++first;

    if (first >= theme.size())
        return m;

    const int origin = (theme[first].onset / tpb) * tpb;

    for (size_t i = first; i < theme.size(); ++i)
    {
        auto t = theme[i];
        t.onset -= origin;

        const bool enough = m.size() >= 4 && t.onset >= tpb;
        const bool tooLong = t.onset >= 3 * tpb || (t.onset >= 2 * tpb && m.size() >= 3); // slow tunes may take a third bar

        if (tooLong || m.size() >= 6 || enough)
            break;

        if (! t.rest)
            m.push_back (t);
    }

    return m;
}

/** The motif's head: its first three notes (the cell that fragmentation repeats). */
Line headOf (const Line& motif, int)
{
    Line h;

    for (auto& t : motif)
        if (h.size() < 3)
            h.push_back (t);

    if (! h.empty())
    {
        const int origin = h.front().onset;

        for (auto& t : h)
            t.onset -= origin;
    }

    return h;
}

int lineSpan (const Line& l)
{
    int end = 0;

    for (auto& t : l)
        end = std::max (end, t.onset + t.dur);

    return end;
}

Line augmentLine (const Line& motif, int startTick, int factor, int limitTicks)
{
    Line out;

    for (auto t : motif)
    {
        t.onset = startTick + t.onset * factor;
        t.dur *= factor;

        if (t.onset >= limitTicks)
            continue;

        t.dur = std::min (t.dur, limitTicks - t.onset);
        out.push_back (t);
    }

    return out;
}

//==============================================================================
// The orchestra

enum class Inst { Vln1, Vln2, Vla, Vc, Cb, Pizz, Trem, Piccolo, Flute, Oboe, Clarinet, Bassoon, Horn, Trumpet, Trombone,
                  Tuba, Harp, Celesta, Mallets, Piano, Choir, Timpani, Perc, Soloist, Count };

enum class Perc { Kit, BassDrum, Cymbals, Snare, Taiko, Toms, TamTam, Triangle };

std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);

    return s;
}

bool has (const std::string& s, const char* what) { return s.find (what) != std::string::npos; }

Inst classify (const TrackSpec& t)
{
    const int p = t.program;
    const auto name = lower (t.name);

    if (t.role == Role::Percussion)          return Inst::Perc;
    if (has (name, "solo") && p >= 40 && p <= 51) return Inst::Soloist;
    if (p == 47)                             return Inst::Timpani;
    if (p == 46)                             return Inst::Harp;
    if (p == 8)                              return Inst::Celesta;
    if (p >= 9 && p <= 14)                   return Inst::Mallets;
    if (p <= 7)                              return Inst::Piano;
    if (p >= 52 && p <= 54)                  return Inst::Choir;
    if (p == 72)                             return Inst::Piccolo;
    if (p == 73 || p == 74 || p == 75)       return Inst::Flute;
    if (p == 68 || p == 69)                  return Inst::Oboe;
    if (p == 71)                             return Inst::Clarinet;
    if (p == 70)                             return Inst::Bassoon;
    if (p == 60 || p == 61)                  return Inst::Horn;
    if (p == 56 || p == 59)                  return Inst::Trumpet;
    if (p == 57)                             return Inst::Trombone;
    if (p == 58)                             return Inst::Tuba;
    if (p == 45)                             return Inst::Pizz;
    if (p == 44)                             return Inst::Trem;
    if (p == 43)                             return Inst::Cb;
    if (p == 42)                             return Inst::Vc;
    if (p == 41)                             return Inst::Vla;

    if (p == 40 || (p >= 48 && p <= 51))
    {
        if (has (name, "violins ii") || has (name, "violin ii") || has (name, "2nd")) return Inst::Vln2;
        if (has (name, "violin"))                                                    return Inst::Vln1;
        if (has (name, "viola"))                                                     return Inst::Vla;
        if (has (name, "cell"))                                                      return Inst::Vc;
        if (has (name, "bass"))                                                      return Inst::Cb;

        const int c = (t.lowNote + t.highNote) / 2;

        if (c >= 70) return Inst::Vln1;
        if (c >= 60) return Inst::Vln2;
        if (c >= 52) return Inst::Vla;
        if (c >= 42) return Inst::Vc;
        return Inst::Cb;
    }

    return Inst::Soloist;
}

Perc percKind (const TrackSpec& t)
{
    const auto name = lower (t.name);

    if (has (name, "bass drum") || has (name, "gran cassa")) return Perc::BassDrum;
    if (has (name, "cymbal"))                                return Perc::Cymbals;
    if (has (name, "snare"))                                 return Perc::Snare;
    if (has (name, "taiko"))                                 return Perc::Taiko;
    if (has (name, "tam"))                                   return Perc::TamTam;
    if (has (name, "triangle"))                              return Perc::Triangle;
    if (has (name, "tom"))                                   return Perc::Toms;
    return Perc::Kit;
}

struct Ensemble
{
    std::vector<int> of[(int) Inst::Count];
    int primary = -1;     // the theme's own instrument
    int arpTrack = -1;    // harp / piano / celesta / koto texture
    int pizzTrack = -1;

    int first (Inst i) const { return of[(int) i].empty() ? -1 : of[(int) i].front(); }
    bool any (Inst i) const  { return ! of[(int) i].empty(); }
};

Ensemble buildEnsemble (const StyleSpec& s)
{
    Ensemble e;

    for (int i = 0; i < (int) s.tracks.size(); ++i)
    {
        const auto& t = s.tracks[(size_t) i];
        e.of[(int) classify (t)].push_back (i);

        if (t.role == Role::Melody && e.primary < 0)
            e.primary = i;

        if (t.role == Role::Arpeggio && e.arpTrack < 0 && t.program != 45)
            e.arpTrack = i;

        if (t.program == 45 && e.pizzTrack < 0)
            e.pizzTrack = i;
    }

    if (e.primary < 0)
    {
        for (Inst pref : { Inst::Horn, Inst::Vln1, Inst::Flute, Inst::Oboe, Inst::Clarinet, Inst::Trumpet, Inst::Soloist, Inst::Vc, Inst::Vln2 })
            if (e.any (pref))
            {
                e.primary = e.first (pref);
                break;
            }
    }

    if (e.primary < 0 && ! s.tracks.empty())
        e.primary = 0;

    return e;
}

struct Flavor
{
    bool drive = false, lyrical = false, pizzDrive = false, hits = false, dark = false, pastoral = false;
    std::string ostinato = "epic";
    double energy = 0.5;
    // Per-cue texture choices (seeded), so two cues in the same style don't share every figure.
    int drumVariant = 0;
    bool pulseStrings = false;
    std::string arpPattern;   // "" = the track's own
};

Flavor flavorOf (const StyleSpec& s, const Ensemble& e)
{
    Flavor f;
    f.energy = s.energy;
    const auto id = lower (s.styleId);

    for (auto& t : s.tracks)
    {
        if (t.role == Role::Ostinato)
        {
            f.drive = true;
            f.ostinato = t.pattern == "auto" ? std::string ("epic") : t.pattern;
        }
    }

    f.drive = f.drive || (s.energy >= 0.72 && (e.any (Inst::Perc) || e.any (Inst::Timpani)));
    f.lyrical = e.arpTrack >= 0;
    f.pizzDrive = e.pizzTrack >= 0;
    f.hits = s.energy >= 0.55;
    f.dark = s.mode == Mode::HarmonicMinor || s.mode == Mode::Phrygian || s.mode == Mode::Locrian
          || has (id, "villain") || has (id, "horror");
    f.pastoral = has (id, "pastoral") || has (id, "whimsical") || has (id, "lullaby");
    return f;
}

//==============================================================================
// Output buckets: (track, group) -> notes. Group 0 = normal, 1 = pizzicato, 2 = tremolo.

struct Buckets
{
    std::map<std::pair<int, int>, Notes> notes;
    std::map<std::pair<int, int>, Controls> cc;

    void add (int track, int group, Notes&& n, Articulation art)
    {
        auto& dst = notes[{ track, group }];

        for (auto& x : n)
        {
            if (x.articulation == Articulation::Default)
                x.articulation = art;

            dst.push_back (x);
        }
    }
};

int groupFor (Articulation a) { return a == Articulation::Pizzicato ? 1 : (a == Articulation::Tremolo ? 2 : 0); }

TrackSpec partSpec (const StyleSpec& s, int track, const CueSection& sec, Role role, const std::string& pattern, double density)
{
    TrackSpec t = s.tracks[(size_t) track];
    t.role = role;
    t.pattern = pattern;
    t.density = density;
    t.entryBar = sec.startBar;
    t.endBar = sec.startBar + sec.bars;
    return t;
}

/** Places a scale-index line on an instrument: whole-line octave placement, then per-note folding. */
Notes placeLine (const Line& line, int sectionStart, const ScaleMap& sm, const TrackSpec& t, const Context& ctx,
                 int relativeShift, int* chosenShift, double velocityScale, double legato)
{
    Notes out;
    double sum = 0.0;
    int count = 0;

    for (auto& n : line)
        if (! n.rest)
        {
            sum += sm.pitch (n.idx);
            ++count;
        }

    if (count == 0)
        return out;

    int shift;

    if (relativeShift != INT_MIN)
    {
        shift = relativeShift;
    }
    else
    {
        const double target = t.lowNote + (t.highNote - t.lowNote) * 0.58;
        shift = 12 * (int) std::lround ((target - sum / count) / 12.0);
    }

    if (chosenShift != nullptr)
        *chosenShift = shift;

    for (auto& n : line)
    {
        if (n.rest)
            continue;

        int p = sm.pitch (n.idx) + shift;

        while (p > t.highNote) p -= 12;
        while (p < t.lowNote)  p += 12;

        RawNote r;
        r.start = sectionStart + n.onset;
        r.length = std::max (kStep / 2, (int) std::lround (n.dur * legato));
        r.pitch = p;
        const bool downbeat = n.onset % ctx.meter.ticksPerBar == 0;
        r.velocity = t.velocity * velocityScale * ctx.energyAt (r.start) * (downbeat ? 1.06 : 0.97);
        r.articulation = Articulation::Legato;
        out.push_back (r);
    }

    return out;
}

/** A counter-melody in real counterpoint with the theme: chord tones on the strong beats (guide
    tones preferred), stepwise passing and neighbour tones between them, moving while the theme
    holds and holding while the theme moves, below the tune, in contrary motion where it can,
    and never a semitone (or minor ninth) against it.
*/
Notes counterLine (const Context& ctx, const CueSection& sec, const TrackSpec& t, const ScaleMap& sm, bool flowing, Rng& rng)
{
    const Meter& m = ctx.meter;
    const int tpb = m.ticksPerBar;
    const int secStart = sec.startBar * tpb;
    const int secEnd = (sec.startBar + sec.bars) * tpb;
    const auto& mel = ctx.leadMelody;
    const int unit = flowing ? (m.compound ? m.beatTicks / 3 : m.beatTicks / 2) : m.beatTicks;
    const int lo = sm.indexAtOrAbove (t.lowNote);
    const int hi = sm.indexAtOrBelow (t.highNote);
    Notes out;

    if (hi <= lo)
        return out;

    auto onsetsIn = [&] (int a, int b)
    {
        int n = 0;

        for (auto& x : mel)
            n += (x.start >= a && x.start < b) ? 1 : 0;

        return n;
    };

    // Where the counter-line speaks.
    std::vector<std::pair<int, bool>> attacks; // tick, strong

    for (int tick = secStart; tick < secEnd; tick += unit)
    {
        const int off = tick % tpb;
        const bool barStart = off == 0;
        const bool chordChange = ctx.chordAt (tick).start == tick;
        const bool strong = barStart || chordChange || (isStrongPosition (m, off, unit) && off % m.beatTicks == 0);
        const bool lastBar = tick >= secEnd - tpb;

        if (lastBar && ! barStart)
            continue;

        const bool themeMoves = onsetsIn (tick, tick + unit) > 0 && onsetsIn (tick - unit, tick) > 0;

        if (strong || ! themeMoves)
            attacks.push_back ({ tick, strong });
    }

    int prevIdx = sm.indexNear ((t.lowNote * 2 + t.highNote) / 3);
    int prevMel = -1;

    for (size_t k = 0; k < attacks.size(); ++k)
    {
        const int tick = attacks[k].first;
        const bool strong = attacks[k].second;
        const int end = k + 1 < attacks.size() ? attacks[k + 1].first : secEnd;
        const auto& ch = ctx.chordAt (tick);
        const int melP = melodyPitchAt (mel, tick);
        const int melDir = (melP >= 0 && prevMel >= 0) ? (melP > prevMel ? 1 : (melP < prevMel ? -1 : 0)) : 0;
        const int length = end - tick;

        int best = -1;
        double bestScore = 1e9;

        for (int idx = lo; idx <= hi; ++idx)
        {
            const int p = sm.pitch (idx);
            const bool chordTone = ch.chord.containsPc (p);
            const int step = std::abs (idx - prevIdx);

            if (strong && ! chordTone)
                continue;

            if (! chordTone && (step != 1 || length > m.beatTicks))
                continue; // non-chord tones only as short passing / neighbour notes

            if (! chordTone && tick % m.beatTicks == 0)
            {
                // On a beat, a passing tone a semitone from the sustained harmony rubs: skip it.
                bool rub = false;

                for (int c : ch.pcs.empty() ? ch.chord.pitchClasses() : ch.pcs)
                    rub = rub || wrapPc (p - c) == 1 || wrapPc (c - p) == 1;

                if (rub)
                    continue;
            }

            double sc = step == 0 ? 1.6 : (step == 1 ? 0.0 : (step == 2 ? 0.7 : 1.3 + 0.6 * step));

            if (chordTone)
            {
                const int iv = wrapPc (p - ch.chord.rootPc);
                sc += (iv == 3 || iv == 4 || iv == 10 || iv == 11) ? -0.7 : (iv == 0 ? 0.2 : -0.2);
            }

            if (melP >= 0)
            {
                const int d = std::abs (melP - p) % 12;

                if (p > melP - 3)                sc += 6.0;  // stay under the tune
                if (d == 1 || d == 11)           sc += 9.0;  // never rub against it
                if (d == 0)                      sc += 1.5;  // and do not just double it

                const int dir = idx > prevIdx ? 1 : (idx < prevIdx ? -1 : 0);

                if (melDir != 0 && dir == -melDir) sc -= 0.5; // contrary motion
                if (melDir != 0 && dir == melDir && d == 7) sc += 2.0; // no parallel fifths
            }

            sc += rng.uniform (0.0, 0.35);

            if (sc < bestScore)
            {
                bestScore = sc;
                best = idx;
            }
        }

        if (best < 0)
            continue;

        const int pitch = sm.pitch (best);

        if (! out.empty() && out.back().pitch == pitch && out.back().start + out.back().length >= tick - 4)
        {
            out.back().length = end - out.back().start - 8; // hold instead of re-striking
        }
        else
        {
            RawNote r;
            r.start = tick;
            r.length = std::max (kStep, length - 8);
            r.pitch = pitch;
            r.velocity = t.velocity * 0.9 * ctx.energyAt (tick) * (strong ? 1.0 : 0.93);
            r.articulation = Articulation::Legato;
            out.push_back (r);
        }

        prevIdx = best;

        if (melP >= 0)
            prevMel = melP;
    }

    return out;
}

/** Chords voiced across a group of instruments (lowest note to the lowest instrument, with
    divisi when there are more voices than players).
*/
enum class ChoirRhythm { Sustain, Pulse, Hits, Heroic };

void choirChords (Context& ctx, const CueSection& sec, const StyleSpec& s, std::vector<int> tracks, ChoirRhythm rhythm,
                  Articulation art, double velocityScale, Buckets& out)
{
    if (tracks.empty())
        return;

    std::sort (tracks.begin(), tracks.end(), [&] (int a, int b)
               { return s.tracks[(size_t) a].lowNote + s.tracks[(size_t) a].highNote < s.tracks[(size_t) b].lowNote + s.tracks[(size_t) b].highNote; });

    int lo = 127, hi = 0;

    for (int t : tracks)
    {
        lo = std::min (lo, s.tracks[(size_t) t].lowNote);
        hi = std::max (hi, s.tracks[(size_t) t].highNote);
    }

    // Only the bass line lives below C3; chords above it stay clear.
    lo = std::max (lo, ctx.hasBassTrack ? 48 : 40);
    hi = std::min (hi, 86);

    if (hi - lo < 14)
        hi = lo + 14;

    const int voices = std::max (3, std::min (5, (int) tracks.size() + 1));
    const Meter& m = ctx.meter;
    const int secStart = sec.startBar * m.ticksPerBar;
    const int secEnd = (sec.startBar + sec.bars) * m.ticksPerBar;
    std::vector<int> prev;

    for (auto& ch : ctx.chordsBetween (secStart, secEnd))
    {
        const auto wanted = wantedPcs (ch.chord, voices, false);
        auto voicing = voiceChord (wanted, ch.chord.rootPc, lo, hi, prev, 26, false);
        prev = voicing;

        // When to sound the chord inside its span.
        std::vector<std::pair<int, int>> hits; // offset, length
        const int len = ch.end - ch.start;

        switch (rhythm)
        {
            case ChoirRhythm::Sustain:
                hits.push_back ({ 0, len });
                break;

            case ChoirRhythm::Pulse:
                for (int x = 0; x < len; x += m.beatTicks)
                    hits.push_back ({ x, std::min (m.beatTicks, len - x) });
                break;

            case ChoirRhythm::Hits:
                for (int x = 0; x < len; x += m.beatTicks * (m.beatsPerBar >= 4 ? 2 : m.beatsPerBar))
                    hits.push_back ({ x, (int) (m.beatTicks * 0.7) });
                break;

            case ChoirRhythm::Heroic:
                for (int bar = 0; bar < len; bar += m.ticksPerBar)
                {
                    if (m.compound || m.beatsPerBar == 3)
                    {
                        hits.push_back ({ bar, m.beatTicks * (m.compound ? 1 : 2) });
                        hits.push_back ({ bar + m.beatTicks * (m.compound ? 1 : 2), m.beatTicks });
                    }
                    else
                    {
                        // dotted quarter, eighth, half: the fanfare figure
                        hits.push_back ({ bar, m.beatTicks * 3 / 2 });
                        hits.push_back ({ bar + m.beatTicks * 3 / 2, m.beatTicks / 2 });
                        hits.push_back ({ bar + m.beatTicks * 2, m.beatTicks * 2 });
                    }
                }
                break;
        }

        for (size_t v = 0; v < voicing.size(); ++v)
        {
            const int ti = tracks[std::min (tracks.size() - 1, v * tracks.size() / voicing.size())];
            const auto& t = s.tracks[(size_t) ti];
            int p = voicing[v];

            while (p > t.highNote && p - 12 >= t.lowNote) p -= 12;
            while (p < t.lowNote && p + 12 <= t.highNote) p += 12;

            if (p < t.lowNote || p > t.highNote)
                continue;

            Notes notes;

            for (size_t h = 0; h < hits.size(); ++h)
            {
                const int start = ch.start + hits[h].first;

                if (start >= ch.end)
                    continue;

                RawNote r;
                r.start = start;
                r.length = std::max (kStep, std::min (hits[h].second, ch.end - start) - 8);
                r.pitch = p;
                const bool accent = hits[h].first % m.ticksPerBar == 0;
                r.velocity = t.velocity * velocityScale * ctx.energyAt (start) * (accent ? 1.05 : 0.92);
                r.articulation = (rhythm == ChoirRhythm::Sustain) ? art : (r.length <= m.beatTicks ? Articulation::Marcato : art);
                notes.push_back (r);
            }

            out.add (ti, groupFor (art), std::move (notes), art);
        }
    }
}

//==============================================================================
// Timpani and percussion

int rollStep (int tempo) { return tempo > 140 ? kStep : kStep / 2; }

void addRoll (Notes& out, int note, int from, int to, int tempo, double v0, double v1)
{
    const int step = rollStep (tempo);

    for (int x = from; x < to; x += step)
    {
        const double f = (double) (x - from) / std::max (1, to - from);
        RawNote r;
        r.start = x;
        r.length = step;
        r.pitch = note;
        r.velocity = v0 + (v1 - v0) * f;
        out.push_back (r);
    }
}

void timpaniPart (Context& ctx, const CueSection& sec, const TrackSpec& t, int track, Buckets& out)
{
    const Meter& m = ctx.meter;
    const double i = sec.intensity();
    const int secStart = sec.startBar * m.ticksPerBar;
    const int secEnd = (sec.startBar + sec.bars) * m.ticksPerBar;
    Notes n;
    int prev = -1;

    auto hit = [&] (int tick, int pitch, double vel, int len)
    {
        RawNote r;
        r.start = tick;
        r.length = len;
        r.pitch = pitch;
        r.velocity = t.velocity * vel * ctx.energyAt (tick);
        n.push_back (r);
    };

    for (auto& ch : ctx.chordsBetween (secStart, secEnd))
    {
        const int root = bassPitchFor (ch.bassRoot(), std::max (38, t.lowNote), std::min (57, t.highNote), prev);
        prev = root;

        if (sec.kind == Kind::Intro || sec.kind == Kind::Contrast || i < 0.4)
            continue;

        if (ch.start % m.ticksPerBar == 0)
            hit (ch.start, root, i >= 0.8 ? 1.0 : 0.82, m.beatTicks);

        if (i >= 0.8 && m.beatsPerBar >= 4 && ch.end - ch.start >= m.ticksPerBar)
            hit (ch.start + 2 * m.beatTicks, fifthAbove (root, 57) > 57 ? root : root, 0.85, m.beatTicks);

        // Pickups into the next bar at full intensity.
        if (i >= 0.9 && ch.end - ch.start >= m.ticksPerBar)
        {
            hit (ch.end - kStep * 2, root, 0.7, kStep);
            hit (ch.end - kStep, root, 0.78, kStep);
        }
    }

    if (sec.kind == Kind::Build)
    {
        const int dom = bassPitchFor (ctx.chordAt (secEnd - 1).bassRoot(), 38, 57, -1);
        addRoll (n, dom, secEnd - m.ticksPerBar, secEnd, ctx.spec.tempo, 45.0, 118.0);
    }

    if (sec.kind == Kind::Outro || sec.kind == Kind::Stinger)
    {
        const int tonic = bassPitchFor (ctx.chordAt (secEnd - 1).bassRoot(), 38, 57, -1);
        const int lastBar = secEnd - m.ticksPerBar;

        if (sec.kind == Kind::Outro && sec.bars >= 2)
            addRoll (n, tonic, lastBar - m.ticksPerBar, lastBar, ctx.spec.tempo, 30.0, 60.0);

        hit (lastBar, tonic, sec.kind == Kind::Stinger ? 1.1 : 0.75, m.beatTicks * 2);
    }

    out.add (track, 0, std::move (n), Articulation::Default);
}

void percussionPart (Context& ctx, const CueSection& sec, const TrackSpec& t, int track, const Flavor& f, Rng& rng, Buckets& out)
{
    const Meter& m = ctx.meter;
    const double i = sec.intensity();
    const int level = i < 0.35 ? 0 : (i < 0.6 ? 1 : (i < 0.84 ? 2 : 3));
    const auto kind = percKind (t);
    const int secStart = sec.startBar * m.ticksPerBar;
    const int secEnd = (sec.startBar + sec.bars) * m.ticksPerBar;
    const bool groove = kind == Perc::Kit && t.pattern != "auto" && t.pattern != "orchestral" && t.pattern != "tribal"
                     && t.pattern != "ambient";

    if (groove)
    {
        // Folk, march, arabic... grooves come from the groove library, thinned by intensity.
        if (level == 0 || sec.kind == Kind::Intro || sec.kind == Kind::Contrast)
            return;

        auto ts = partSpec (ctx.spec, track, sec, Role::Percussion, t.pattern, 0.25 + 0.2 * level);
        out.add (track, 0, generateDrums (ctx, ts, rng), Articulation::Default);
        return;
    }

    Notes n;

    auto hit = [&] (int tick, int note, double vel, int len = kStep)
    {
        RawNote r;
        r.start = tick;
        r.length = len;
        r.pitch = note;
        r.velocity = t.velocity * vel * ctx.energyAt (tick);
        n.push_back (r);
    };

    const bool bassDrum = kind == Perc::BassDrum || kind == Perc::Kit;
    const bool drums = kind == Perc::Taiko || kind == Perc::Toms || (kind == Perc::Kit && (f.drive || t.pattern == "tribal"));
    const bool cymbals = kind == Perc::Cymbals || kind == Perc::Kit;
    const bool snare = kind == Perc::Snare || kind == Perc::Kit;

    // 16th-step patterns (4/4); other meters use beat-based figures.
    static const char* lowPatterns[]  = { "x..x..x.x..x..x.", "x...x.x.x...x.x.", "x.x...x.x.x...x.", "x..x...xx..x..x.", "x...x...x...x.x." };
    static const char* highPatterns[] = { "......x.......x.", "....x.......x...", "..x...x...x...x.", "......x...x...x.", "....x.......x.x." };
    const char* lowDrums = lowPatterns[f.drumVariant % 5];
    const char* highDrums = highPatterns[(f.drumVariant * 3 + 1) % 5];
    const bool fourFour = m.beatsPerBar == 4 && ! m.compound;

    for (int bar = sec.startBar; bar < sec.startBar + sec.bars; ++bar)
    {
        const int bs = bar * m.ticksPerBar;
        const int inSec = bar - sec.startBar;
        const bool lastBarOfPhrase = (inSec + 1) % 4 == 0;

        if (sec.kind == Kind::Outro || sec.kind == Kind::Stinger)
        {
            if (bar == sec.startBar + sec.bars - 1)
            {
                if (bassDrum) hit (bs, 35, 1.0, m.beatTicks * 2);
                if (cymbals)  hit (bs, 49, 0.95, m.ticksPerBar);
                if (kind == Perc::TamTam || (cymbals && f.dark)) hit (bs, 52, 0.8, m.ticksPerBar);
            }

            continue;
        }

        if (bassDrum && level >= 1 && (level >= 2 || inSec % 2 == 0))
        {
            hit (bs, 35, level >= 3 ? 1.0 : 0.8, m.beatTicks);

            if (level >= 3 && m.beatsPerBar >= 4)
                hit (bs + 2 * m.beatTicks, 35, 0.85, m.beatTicks);
        }

        if (drums && level >= 2)
        {
            if (fourFour)
            {
                for (int st = 0; st < 16; ++st)
                {
                    if (lowDrums[st] == 'x')
                        hit (bs + st * kStep, kind == Perc::Taiko ? 41 : 43, (st % 4 == 0 ? 1.0 : 0.82) * (level >= 3 ? 1.0 : 0.8));

                    if (level >= 3 && highDrums[st] == 'x')
                        hit (bs + st * kStep, 45, 0.85);
                }
            }
            else
            {
                for (int b = 0; b < m.beatsPerBar; ++b)
                    hit (bs + b * m.beatTicks, b == 0 ? 41 : 43, b == 0 ? 1.0 : 0.8);
            }
        }

        if (snare && level >= 2 && lastBarOfPhrase && sec.kind != Kind::Build)
            addRoll (n, 38, bs + m.ticksPerBar - m.beatTicks, bs + m.ticksPerBar, ctx.spec.tempo, 40.0 * t.velocity / 100.0, 95.0 * t.velocity / 100.0);

        if (cymbals && level >= 3 && inSec % 4 == 0 && inSec > 0)
            hit (bs, 49, 0.9, m.beatTicks * 2);

        if (kind == Perc::Triangle && (level == 1 || f.pastoral) && level < 3)
            hit (bs, 81, 0.6, m.beatTicks);

        if (kind == Perc::TamTam && sec.kind == Kind::Climax && inSec == 0)
            hit (bs, 52, 0.9, m.ticksPerBar * 2);
    }

    if (sec.kind == Kind::Build)
    {
        // The build's last bar: snare and suspended-cymbal swell into the hit.
        if (snare)
            addRoll (n, 38, secEnd - m.ticksPerBar, secEnd, ctx.spec.tempo, 30.0, 110.0);

        if (cymbals)
            addRoll (n, 51, secEnd - m.ticksPerBar, secEnd, ctx.spec.tempo, 20.0, 95.0);
    }

    (void) secStart;
    out.add (track, 0, std::move (n), Articulation::Default);
}

//==============================================================================
// Melody per section

struct SectionLine
{
    Line line;           // the lead line (scale indices in the section's key)
    Line imitation;      // canon / call-and-response line, if any
    ScaleMap sm;
    int lo = 0, hi = 0;  // index bounds the line was written in
};

ScaleMap scaleFor (const CueSection& sec)
{
    return { modeInfo (sec.mode).melodyScale, tonicPitchFor (sec.keyRoot) };
}

//==============================================================================
// Reharmonisation: restatements keep the melody, the chords underneath change.

/** Chords a restatement may be reharmonised with, per mode (numerals count the mode's own
    degrees, so modal colour is kept: Dorian's major IV, Mixolydian's bVII, Lydian's II). */
std::vector<std::string> reharmCandidates (Mode mode)
{
    switch (mode)
    {
        case Mode::Major:         return { "I", "ii", "iii", "IV", "V", "vi", "iv", "bVI", "bVII", "V7/V", "V7/vi", "V7/ii", "V7/IV", "ii7", "IVmaj7", "vi7" };
        case Mode::Minor:         return { "i", "iio", "III", "iv", "v", "V", "VI", "VII", "V7/iv", "V7/III", "V7/VI", "iv7", "VImaj7" };
        case Mode::HarmonicMinor: return { "i", "iio", "iv", "V", "VI", "V7/iv", "iv7" };
        case Mode::Dorian:        return { "i", "ii", "III", "IV", "v", "VII", "i7", "IV7" };
        case Mode::Mixolydian:    return { "I", "ii", "IV", "v", "vi", "VII", "IVmaj7" };
        case Mode::Lydian:        return { "I", "II", "iii", "V", "vi", "vii", "Imaj7" };
        case Mode::Phrygian:      return { "i", "II", "III", "iv", "VI", "vii" };
        default:                  return {};
    }
}

void reharmonise (Harmony& h, const Line& theme, const CueSection& sec, const Language& L, double amount, Rng& rng, const Meter& m)
{
    const auto candidates = reharmCandidates (sec.mode);

    if (candidates.empty())
        return;

    const ScaleMap sm = scaleFor (sec);
    const int bars = (int) h.size();

    // Keep the last two bars of each 8-bar unit: the cadence is the phrase's punctuation.
    auto protectedBar = [&] (int b) { return (b % 8) >= 6 || b >= bars - 2; };

    Chord prevChord;
    bool havePrev = false;
    int changed = 0, fallbackBar = -1;
    double fallbackScore = -1e9;
    std::string fallbackChord;

    for (int b = 0; b < bars; ++b)
    {
        if (h[(size_t) b].chords.size() != 1 || protectedBar (b))
        {
            parseChordSymbol (h[(size_t) b].chords.back(), sec.keyRoot, sec.mode, prevChord);
            havePrev = true;
            continue;
        }

        // Melody notes on the strong beats of this bar.
        std::vector<int> strongPcs;

        for (auto& t : theme)
        {
            if (t.rest || t.onset < (b % 8) * m.ticksPerBar || t.onset >= ((b % 8) + 1) * m.ticksPerBar)
                continue;

            const int off = t.onset % m.ticksPerBar;

            if (isStrongPosition (m, off, t.dur))
                strongPcs.push_back (wrapPc (sm.pitch (t.idx)));
        }

        if (strongPcs.empty())
        {
            parseChordSymbol (h[(size_t) b].chords[0], sec.keyRoot, sec.mode, prevChord);
            havePrev = true;
            continue;
        }

        const bool tryThisBar = rng.chance (amount);

        const std::string original = h[(size_t) b].chords[0];
        Chord nextChord;
        const bool haveNext = b + 1 < bars && parseChordSymbol (h[(size_t) b + 1].chords[0], sec.keyRoot, sec.mode, nextChord);

        std::string best = original;
        double bestScore = -1e9;
        std::string bestOther;
        double bestOtherScore = -1e9;

        for (auto& cand : candidates)
        {
            Chord c;

            if (! parseChordSymbol (cand, sec.keyRoot, sec.mode, c) || ! c.containsPc (strongPcs[0]))
                continue;

            // The theme is fixed: new harmony must not rub against any of its strong notes
            // (otherwise the clash resolver would have to bend the tune).
            bool rubs = false;

            for (int pc : strongPcs)
                if (! c.containsPc (pc))
                    for (int cpc : c.pitchClasses())
                        rubs = rubs || wrapPc (pc - cpc) == 1 || wrapPc (cpc - pc) == 1;

            if (rubs && cand != original)
                continue;

            double score = 0.0;

            for (size_t k = 1; k < strongPcs.size(); ++k)
                score += c.containsPc (strongPcs[k]) ? 1.0 : -0.8;

            score += cand != original ? 0.9 : 0.0;

            if (havePrev)
            {
                const int motion = wrapPc (c.rootPc - prevChord.rootPc);

                if (motion == 5 || motion == 7)       score += 0.6;
                else if (motion == 2 || motion == 10) score += 0.3;
                else if (motion == 0)                 score -= 0.7;
            }

            if (c.hasInterval (6) && c.hasInterval (3))
                score -= 0.6; // diminished colour sparingly

            if (cand.find ('/') != std::string::npos)
            {
                // A secondary dominant must actually resolve to its target.
                Chord target;
                const auto slash = cand.find ('/');
                const bool ok = haveNext && parseChordSymbol (cand.substr (slash + 1), sec.keyRoot, sec.mode, target)
                             && target.rootPc == nextChord.rootPc;
                score += ok ? 0.9 : -3.0;
            }

            score += rng.uniform (0.0, 0.5);

            if (score > bestScore)
            {
                bestScore = score;
                best = cand;
            }

            if (cand != original && score > bestOtherScore)
            {
                bestOtherScore = score;
                bestOther = cand;
            }
        }

        if (! bestOther.empty() && bestOtherScore > fallbackScore)
        {
            fallbackScore = bestOtherScore;
            fallbackBar = b;
            fallbackChord = bestOther;
        }

        if (tryThisBar)
        {
            changed += best != original ? 1 : 0;
            h[(size_t) b].chords[0] = best;
        }

        parseChordSymbol (h[(size_t) b].chords[0], sec.keyRoot, sec.mode, prevChord);
        havePrev = true;
    }

    // A restatement is always recoloured somewhere: if chance changed nothing, take the
    // best-fitting alternative found.
    if (changed == 0 && fallbackBar >= 0 && fallbackScore > -0.5)
        h[(size_t) fallbackBar].chords[0] = fallbackChord;
}

//==============================================================================
// Transitions

/** A scale run in the next section's key, landing on `target` on the downbeat. */
Notes scaleRun (int target, const CueSection& next, int endTick, int lengthTicks, int noteTicks, double velocity, int lowest)
{
    Notes out;
    const auto& scale = modeInfo (next.mode).harmonyScale;
    const ScaleMap sm { scale, tonicPitchFor (next.keyRoot) };
    int idx = sm.indexAtOrBelow (target - 1);
    const int count = std::max (2, lengthTicks / noteTicks);
    std::vector<int> pitches;

    for (int k = 0; k < count; ++k)
        pitches.push_back (sm.pitch (idx - k));

    std::reverse (pitches.begin(), pitches.end());

    for (int k = 0; k < count; ++k)
    {
        if (pitches[(size_t) k] < lowest)
            continue;

        RawNote r;
        r.start = endTick - lengthTicks + k * noteTicks;
        r.length = noteTicks;
        r.pitch = pitches[(size_t) k];
        r.velocity = velocity * (0.7 + 0.35 * (double) k / count);
        r.articulation = Articulation::Legato;
        out.push_back (r);
    }

    return out;
}

void tuttiHit (Context& ctx, const CueSection& sec, const StyleSpec& s, const Ensemble& e, const Flavor& f, Buckets& out)
{
    const Meter& m = ctx.meter;
    const int tick = sec.startBar * m.ticksPerBar;
    const auto& ch = ctx.chordAt (tick + 1);
    const int pre = tick - 2 * m.beatTicks;

    if (pre <= 0)
        return;

    // Swell: timpani roll on the bass of the chord that is sounding, then the hit on the new root.
    if (e.any (Inst::Timpani))
    {
        const int ti = e.first (Inst::Timpani);
        const auto& t = s.tracks[(size_t) ti];
        const int root = bassPitchFor (ch.bassRoot(), std::max (38, t.lowNote), std::min (57, t.highNote), -1);
        const int rollPitch = bassPitchFor (ctx.chordAt (tick - 1).bassRoot(), std::max (38, t.lowNote), std::min (57, t.highNote), root);
        Notes n;
        addRoll (n, rollPitch, pre, tick, s.tempo, 40.0, 112.0);
        RawNote h;
        h.start = tick;
        h.length = m.beatTicks;
        h.pitch = root;
        h.velocity = 124.0;
        n.push_back (h);
        out.add (ti, 0, std::move (n), Articulation::Default);
    }

    // Percussion: bass drum, crash (and tam-tam in dark music) on the downbeat.
    for (int ti : e.of[(int) Inst::Perc])
    {
        const auto kind = percKind (s.tracks[(size_t) ti]);
        Notes n;

        auto add = [&] (int note, double vel, int len)
        {
            RawNote r;
            r.start = tick;
            r.length = len;
            r.pitch = note;
            r.velocity = vel;
            n.push_back (r);
        };

        if (kind == Perc::BassDrum || kind == Perc::Kit)                     add (35, 120.0, m.beatTicks);
        if (kind == Perc::Cymbals || kind == Perc::Kit)                      add (49, 118.0, m.beatTicks * 2);
        if (kind == Perc::TamTam || ((kind == Perc::Cymbals) && f.dark))     add (52, 105.0, m.ticksPerBar);
        if (kind == Perc::Taiko || kind == Perc::Toms)                       add (41, 120.0, m.beatTicks);

        if (kind == Perc::Snare || kind == Perc::Kit)
        {
            Notes roll;
            addRoll (roll, 38, pre, tick, s.tempo, 35.0, 105.0);
            n.insert (n.end(), roll.begin(), roll.end());
        }

        if (kind == Perc::Cymbals)
        {
            Notes swell;
            addRoll (swell, 51, tick - m.ticksPerBar, tick, s.tempo, 18.0, 90.0);
            n.insert (n.end(), swell.begin(), swell.end());
        }

        out.add (ti, 0, std::move (n), Articulation::Default);
    }

    // Brass and low strings: a marcato chord on the downbeat.
    const int length = (int) (m.beatTicks * 0.75);

    for (Inst inst : { Inst::Trombone, Inst::Tuba, Inst::Horn, Inst::Vc, Inst::Cb })
    {
        for (int ti : e.of[(int) inst])
        {
            const auto& t = s.tracks[(size_t) ti];
            std::vector<int> pitches;
            const bool upper = inst == Inst::Horn || inst == Inst::Trombone;
            const bool bassBelow = e.any (Inst::Cb) || e.any (Inst::Tuba);
            const int floor = upper && bassBelow ? std::max (t.lowNote, inst == Inst::Horn ? 53 : 46) : t.lowNote;
            const int root = bassPitchFor (upper ? ch.chord.rootPc : ch.bassRoot(), floor, t.highNote, -1);
            pitches.push_back (root);

            if (upper)
            {
                // Open spacing low down (fifths and octaves), closer spacing above C4.
                for (int p = root + 1; p <= std::min (t.highNote, root + 16) && pitches.size() < 3; ++p)
                    if (ch.chord.containsPc (p) && p - pitches.back() >= (pitches.back() < 60 ? 5 : 3))
                        pitches.push_back (p);
            }
            else if (root + 12 <= t.highNote)
            {
                pitches.push_back (root + 12);
            }

            Notes n;

            for (int p : pitches)
            {
                RawNote r;
                r.start = tick;
                r.length = length;
                r.pitch = p;
                r.velocity = t.velocity * 1.15;
                r.articulation = Articulation::Marcato;
                n.push_back (r);
            }

            out.add (ti, 0, std::move (n), Articulation::Marcato);
        }
    }
}

} // namespace

//==============================================================================
Composition composeCue (const StyleSpec& specIn, uint64_t seed)
{
    StyleSpec spec = specIn;
    normalizeSpec (spec);
    const bool loopable = ! spec.resolveEnding;
    spec.resolveEnding = true; // the outro / stinger always resolves; looping is done with markers

    Rng rng (seed ^ 0xC0E5EEDull);
    const Ensemble E = buildEnsemble (spec);
    Flavor F = flavorOf (spec, E);

    {
        Rng fr (seed ^ 0xF1A7ull);
        const bool compound = spec.tsDen == 8;

        if (F.drive && fr.chance (0.5))
            F.ostinato = fr.pick (compound || spec.tempo >= 130 ? std::vector<std::string> { "epic", "pulse", "gallop" }
                                                                : std::vector<std::string> { "epic", "pulse" });

        F.drumVariant = fr.range (0, 4);
        F.pulseStrings = F.drive && fr.chance (0.3);

        if (fr.chance (0.4))
            F.arpPattern = fr.pick (std::vector<std::string> { "up", "updown", "random", "down" });
    }
    const int target = targetBarsFor (spec);
    auto sections = planForm (spec, target, loopable, spec.energy, rng);

    int bars = 0;

    for (auto& s : sections)
        bars = std::max (bars, s.startBar + s.bars);

    spec.bars = bars;

    Context ctx (spec);
    const Meter& m = ctx.meter;
    ctx.bars = bars;
    ctx.totalTicks = bars * m.ticksPerBar;
    ctx.tonicPitch = tonicPitchFor (spec.keyRoot);

    for (auto& t : spec.tracks)
        ctx.hasBassTrack = ctx.hasBassTrack || t.role == Role::Bass || classify (t) == Inst::Cb;

    ctx.barIntensity.assign ((size_t) bars, 0.5);

    for (auto& sec : sections)
    {
        for (int b = 0; b < sec.bars; ++b)
        {
            const double f = sec.bars > 1 ? (double) b / (sec.bars - 1) : 0.0;
            ctx.barIntensity[(size_t) (sec.startBar + b)] = sec.i0 + (sec.i1 - sec.i0) * f;
        }

        ctx.sections.push_back ({ sec.startBar, sec.bars, kindLetter (sec.kind), sec.keyRoot, sec.intensity() });
    }

    //==========================================================================
    // Harmony, first pass: everything up to and including the first statement of the theme.
    const Language home = languageFor (spec.mode, spec.progression, rng);
    std::map<int, Language> languages; // by section index
    std::vector<Harmony> harmonies (sections.size());

    auto languageAt = [&] (size_t si) -> Language
    {
        const auto& sec = sections[si];

        if (sec.mode == spec.mode)
        {
            Language L = home;

            if (sec.kind == Kind::Contrast && ! spec.progressionB.empty() && sec.keyRoot == spec.keyRoot)
                L.base = spec.progressionB;

            return L;
        }

        static const std::vector<std::vector<std::string>> majorBank { { "I", "iii", "IV", "I" }, { "IV", "I", "ii", "V" }, { "vi", "IV", "I", "V" }, { "I", "vi", "IV", "V" } };
        static const std::vector<std::vector<std::string>> minorBank { { "i", "VI", "III", "VII" }, { "iv", "i", "VI", "V" }, { "i", "iv", "VI", "V" } };
        const auto& bank = isMinorish (sec.mode) ? minorBank : majorBank;
        return languageFor (sec.mode, bank[(size_t) rng.range (0, (int) bank.size() - 1)], rng);
    };

    for (size_t si = 0; si < sections.size(); ++si)
        languages[(int) si] = languageAt (si);

    auto commit = [&] (size_t si)
    {
        const auto& sec = sections[si];
        const auto& h = harmonies[si];
        Rng colour (seed ^ (0xC010ull + si * 977));

        for (int b = 0; b < (int) h.size(); ++b)
        {
            const auto& plan = h[(size_t) b];
            const int barStart = (sec.startBar + b) * m.ticksPerBar;
            const int split = plan.chords.size() > 1 ? ((m.beatsPerBar + 1) / 2) * m.beatTicks : m.ticksPerBar;

            for (size_t c = 0; c < plan.chords.size(); ++c)
            {
                ChordEvent e;
                e.start = barStart + (c == 0 ? 0 : split);
                e.end = c + 1 < plan.chords.size() ? barStart + split : barStart + m.ticksPerBar;
                e.keyRoot = sec.keyRoot;
                e.roman = plan.chords[c];

                if (! parseChordSymbol (e.roman, sec.keyRoot, sec.mode, e.chord))
                    e.chord = diatonicChord (0, sec.keyRoot, sec.mode, false);

                const bool cadential = b >= (int) h.size() - 1;

                if (e.chord.intervals.size() == 3 && ! cadential && e.roman.find ("sus") == std::string::npos)
                {
                    if (spec.complexity > 0.35 && colour.chance ((spec.complexity - 0.25) * 1.1))
                        addDiatonicSeventh (e.chord, sec.keyRoot, sec.mode);

                    if (spec.complexity > 0.65 && colour.chance ((spec.complexity - 0.55) * 1.2))
                        addDiatonicNinth (e.chord, sec.keyRoot, sec.mode);
                }

                if (plan.pedal == 1)
                    e.bassPc = wrapPc (sec.keyRoot);
                else if (plan.pedal == 2)
                    e.bassPc = wrapPc (sec.keyRoot + 7);

                if (e.bassPc == e.chord.rootPc)
                    e.bassPc = -1;

                e.pcs = e.chord.pitchClasses();
                ctx.chords.push_back (e);
            }
        }

        std::sort (ctx.chords.begin(), ctx.chords.end(), [] (const ChordEvent& a, const ChordEvent& b) { return a.start < b.start; });
    };

    int statementIndex = -1;

    for (size_t si = 0; si < sections.size(); ++si)
    {
        harmonies[si] = sectionHarmony (sections[si], languages[(int) si], languages[(int) si], spec.energy, spec.complexity, F.drive, rng);

        if (sections[si].kind == Kind::Statement && statementIndex < 0)
            statementIndex = (int) si;
    }

    // Commit the intro and the first statement, compose the theme over them.
    for (size_t si = 0; si < sections.size(); ++si)
    {
        commit (si);

        if ((int) si == statementIndex)
            break;
    }

    if (statementIndex < 0)
        statementIndex = 0;

    const auto& primarySpec = spec.tracks[(size_t) std::max (0, E.primary)];
    const auto& stmt = sections[(size_t) statementIndex];
    const ScaleMap homeScale = scaleFor (stmt);
    const int sn = homeScale.size();
    // The theme lives in the instrument's singing register: the top two thirds of its range,
    // and above the bass register when there is a bass part.
    const int span = primarySpec.highNote - primarySpec.lowNote;
    int singLo = primarySpec.lowNote + (int) std::lround (span * 0.3);
    const int singHi = primarySpec.highNote - (int) std::lround (span * 0.05);

    if (ctx.hasBassTrack)
        singLo = std::max (singLo, std::min (50, singHi - 17));

    int tlo = homeScale.indexAtOrAbove (singLo);
    int thi = homeScale.indexAtOrBelow (singHi);

    if (thi - tlo < sn + 3)
        thi = tlo + sn + 3;

    MelStyle style = MelStyle::Lyrical;

    if (primarySpec.pattern == "rhythmic" || (primarySpec.pattern == "auto" && primarySpec.density > 0.68))
        style = MelStyle::Rhythmic;

    const double themeDensity = std::max (0.3, std::min (0.7, primarySpec.density));
    const int themeBars = std::min (8, stmt.bars);
    const int stmtStart = stmt.startBar * m.ticksPerBar;
    Line theme = composePeriod (ctx, stmtStart, themeBars, homeScale, stmt.keyRoot, tlo, thi, style, themeDensity, spec.energy, 1, true, rng);

    // A theme supplied with the spec (e.g. written by Claude) replaces the opening bars.
    if (! spec.theme.empty())
    {
        Line given;
        int tick = 0;

        for (auto& tn : spec.theme)
        {
            const int dur = std::max (kStep / 2, (int) std::lround (tn.beats * kPpq));

            if (tick >= themeBars * m.ticksPerBar)
                break;

            given.push_back ({ tick, dur, tn.degree, tn.rest });
            tick += dur;
        }

        const int givenEnd = ((tick + m.beatTicks - 1) / m.beatTicks) * m.beatTicks;
        Line merged;

        for (auto& g : given)
            merged.push_back (g);

        for (auto& t : theme)
            if (t.onset >= givenEnd)
                merged.push_back (t);

        // Centre the given theme in the instrument's register.
        double sum = 0.0;
        int count = 0;

        for (auto& g : given)
            if (! g.rest)
            {
                sum += g.idx;
                ++count;
            }

        if (count > 0)
        {
            const int shift = sn * (int) std::lround (((tlo + thi) * 0.5 - sum / count) / sn);

            for (auto& g : merged)
                if (g.onset < givenEnd)
                    g.idx += shift;
        }

        theme = merged;
    }

    const Line motif = motifOf (theme, m.ticksPerBar);
    const Line head = headOf (motif, m.beatTicks * 2);

    //==========================================================================
    // Harmony, second pass: reharmonised restatements and the rest of the form.
    const Harmony statementHarmony = harmonies[(size_t) statementIndex];

    // Do the theme's strong-beat notes in bar `themeBar` fit these chords (one per half bar)?
    auto themeFits = [&] (int themeBar, const std::vector<std::string>& chords, const CueSection& sec)
    {
        const ScaleMap sm = scaleFor (sec);

        for (auto& t : theme)
        {
            if (t.rest || t.onset < themeBar * m.ticksPerBar || t.onset >= (themeBar + 1) * m.ticksPerBar)
                continue;

            const int off = t.onset % m.ticksPerBar;

            if (! isStrongPosition (m, off, t.dur) && t.dur < m.beatTicks)
                continue;

            const int split = ((m.beatsPerBar + 1) / 2) * m.beatTicks;
            std::vector<size_t> spans { chords.size() > 1 && off >= split ? (size_t) 1 : (size_t) 0 };

            if (chords.size() > 1 && off < split && off + t.dur > split)
                spans.push_back (1);

            for (size_t c : spans)
            {
                Chord ch;

                if (! parseChordSymbol (chords[c], sec.keyRoot, sec.mode, ch) || ! ch.containsPc (sm.pitch (t.idx)))
                    return false;
            }
        }

        return true;
    };

    for (size_t si = (size_t) statementIndex + 1; si < sections.size(); ++si)
    {
        auto& sec = sections[si];

        if ((sec.kind == Kind::Restatement || sec.kind == Kind::Climax) && sec.mode == stmt.mode && ! statementHarmony.empty())
        {
            // The theme comes back over the phrase structure it was written for...
            const auto& L = languages[(int) si];
            Harmony h;

            for (int b = 0; b < sec.bars; ++b)
                h.push_back (statementHarmony[(size_t) b % statementHarmony.size()]);

            if (sec.bars % themeBars != 0)
                h.back() = BarPlan { { L.tonic }, 0 };

            // ...with an epic (bVI - bVII - I) cadence for a big climax, where the melody allows it.
            if (sec.kind == Kind::Climax && (spec.energy >= 0.65 || F.drive) && L.epic.size() >= 3 && sec.bars >= 2
                && sec.bars % themeBars == 0)
            {
                const std::vector<std::string> penult { L.epic[0], L.epic[1] };
                const int tb = (sec.bars - 2) % themeBars;

                if (themeFits (tb, penult, sec) && themeFits ((sec.bars - 1) % themeBars, { L.epic.back() }, sec))
                {
                    h[(size_t) sec.bars - 2] = BarPlan { penult, 0 };
                    h[(size_t) sec.bars - 1] = BarPlan { { L.epic.back() }, 0 };
                }
            }

            harmonies[si] = h;
        }

        if (sec.kind == Kind::Restatement || sec.kind == Kind::Climax)
            reharmonise (harmonies[si], theme, sec, languages[(int) si], 0.35 + 0.4 * spec.complexity + (sec.kind == Kind::Climax ? 0.1 : 0.0), rng, m);

        commit (si);
    }

    //==========================================================================
    // Melody lines for every section.
    std::vector<SectionLine> lines (sections.size());
    const int tpb = m.ticksPerBar;

    for (size_t si = 0; si < sections.size(); ++si)
    {
        const auto& sec = sections[si];
        auto& L = lines[si];
        L.sm = scaleFor (sec);
        const int n = L.sm.size();
        L.lo = tlo;
        L.hi = thi;
        const int start = sec.startBar * tpb;
        const int len = sec.bars * tpb;

        switch (sec.kind)
        {
            case Kind::Intro:
            {
                // The motif in augmentation, foreshadowing the theme.
                const int from = sec.bars >= 4 ? tpb : 0;
                L.line = augmentLine (motif, from, 2, len - (sec.bars > 2 ? tpb : 0));
                break;
            }

            case Kind::Statement:
                L.line = theme;

                for (int b = themeBars; b < sec.bars; ++b)
                    for (auto t : theme)
                        if (t.onset >= (b % themeBars) * tpb && t.onset < (b % themeBars + 1) * tpb)
                        {
                            t.onset += (b - b % themeBars) * tpb;
                            L.line.push_back (t);
                        }
                break;

            case Kind::Restatement:
            case Kind::Climax:
            {
                // The theme, unchanged, over its new harmony (and in a new key for the climax).
                for (int b = 0; b < sec.bars; ++b)
                {
                    const int src = b % themeBars;
                    const bool lastBar = b == sec.bars - 1;

                    for (auto t : theme)
                    {
                        if (t.onset < src * tpb || t.onset >= (src + 1) * tpb)
                            continue;

                        t.onset += (b - src) * tpb;
                        L.line.push_back (t);
                    }

                    if (lastBar && src != themeBars - 1)
                    {
                        // A shortened statement still has to cadence: replace the bar with the tonic.
                        L.line.erase (std::remove_if (L.line.begin(), L.line.end(), [&] (const TNote& t) { return t.onset >= b * tpb; }), L.line.end());
                        L.line.push_back ({ b * tpb, tpb, cadenceIndex (L.sm, L.line.empty() ? (tlo + thi) / 2 : L.line.back().idx, tlo, thi,
                                                                       ctx.chordAt (start + b * tpb + 1), CadencePref::Tonic, sec.keyRoot), false });
                    }
                }

                break;
            }

            case Kind::Development:
            {
                // Sequence: each repetition follows the harmony's root motion, so the motif keeps
                // its relationship to every chord; then fragmentation (the head cell, twice per unit)
                // and liquidation (its first two notes, rising), then a retransition on the dominant.
                const int dominantIdx = n * ((tlo + thi) / 2 / n) + (n >= 7 ? 4 : 3);
                const int body = std::max (1, sec.bars - 2);
                const int unitBars = std::max (1, (lineSpan (motif) + tpb - 1) / tpb);

                auto degreeOf = [&] (int pc, const ScaleMap& sc)
                {
                    int best = 0, bestDist = 99;

                    for (int d = 0; d < sc.size(); ++d)
                    {
                        const int dist = std::abs (wrapPc (sc.pitch (d) - pc + 6) - 6);

                        if (dist < bestDist)
                        {
                            bestDist = dist;
                            best = d;
                        }
                    }

                    return best;
                };

                const int motifTick = stmtStart + (motif.empty() ? 0 : motif.front().onset) + 1;
                const int themeRoot = degreeOf (ctx.chordAt (motifTick).chord.rootPc, homeScale);
                int prevShift = 0;

                // The transposition that follows the chord at `tick`, in the octave nearest the last
                // one that keeps the whole fragment inside the singing range (register jumps happen
                // between fragments, never inside one).
                auto shiftAt = [&] (int tick, const Line& material, int extra)
                {
                    const int root = degreeOf (ctx.chordAt (start + tick + 1).chord.rootPc, L.sm);
                    int lowIdx = 1 << 20, highIdx = -(1 << 20);

                    for (auto& t : material)
                    {
                        lowIdx = std::min (lowIdx, t.idx + extra);
                        highIdx = std::max (highIdx, t.idx + extra);
                    }

                    int best = 0, bestDist = 1 << 20;
                    bool bestFits = false;

                    for (int k = -3; k <= 3; ++k)
                    {
                        const int cand = root - themeRoot + k * n;
                        const bool fits = lowIdx + cand >= tlo && highIdx + cand <= thi;
                        const int dist = std::abs (cand - prevShift);

                        if ((fits && ! bestFits) || (fits == bestFits && dist < bestDist))
                        {
                            bestFits = fits;
                            bestDist = dist;
                            best = cand;
                        }
                    }

                    prevShift = best;
                    return best + extra;
                };

                const int units = std::max (1, body / unitBars);
                const int firstOffset = motif.empty() ? 0 : motif.front().onset;
                const int headSpan = std::max (m.beatTicks, ((lineSpan (head) + m.beatTicks - 1) / m.beatTicks) * m.beatTicks);

                for (int u = 0; u < units; ++u)
                {
                    const int unitStart = u * unitBars * tpb;
                    const int phase = units >= 3 ? (u * 3) / units : (u == 0 ? 0 : 1); // 0 sequence, 1 fragment, 2 liquidate

                    if (phase == 0)
                    {
                        const int shift = shiftAt (unitStart + firstOffset, motif, 0);

                        for (auto t : motif)
                        {
                            t.onset += unitStart;
                            t.idx += shift;
                            L.line.push_back (t);
                        }
                    }
                    else
                    {
                        Line cell = head;

                        if (phase == 2 && cell.size() > 2)
                            cell.resize (2);

                        const int span = phase == 2 ? std::max (m.beatTicks, ((lineSpan (cell) + m.beatTicks - 1) / m.beatTicks) * m.beatTicks)
                                                    : headSpan;
                        int rep = 0;

                        for (int at = unitStart + (phase == 1 ? firstOffset % tpb : 0); at + span <= unitStart + unitBars * tpb; at += span, ++rep)
                        {
                            const int shift = shiftAt (at, cell, phase == 2 ? rep : 0);

                            for (auto t : cell)
                            {
                                t.onset += at;
                                t.idx += shift;
                                L.line.push_back (t);
                            }
                        }
                    }
                }

                for (int b = units * unitBars; b < sec.bars; ++b)
                    L.line.push_back ({ b * tpb, tpb, dominantIdx + (b == sec.bars - 1 ? 0 : 2), false });

                // (no chord-tone snapping: it would bend the motif; clashes are resolved below)
                // The answer: the same material half a bar later, an octave down.
                for (auto t : L.line)
                {
                    t.onset += tpb / 2;
                    t.idx -= n;

                    if (t.onset + t.dur <= (sec.bars - 2) * tpb)
                        L.imitation.push_back (t);
                }

                for (auto& t : L.imitation)
                {
                    while (t.idx < tlo - n) t.idx += n;
                    while (t.idx > thi - n) t.idx -= n;
                }
                break;
            }

            case Kind::Contrast:
            {
                // A second theme: new material, more lyrical, opening downwards.
                Rng contrastRng (seed ^ 0xB5EC0DDull);
                const int clo = L.sm.indexAtOrAbove (primarySpec.lowNote);
                const int chi = std::max (clo + n + 3, L.sm.indexAtOrBelow (primarySpec.highNote));
                L.lo = clo;
                L.hi = chi;
                L.line = composePeriod (ctx, start, std::min (8, sec.bars), L.sm, sec.keyRoot, clo, chi, MelStyle::Lyrical,
                                        std::max (0.25, themeDensity - 0.15), spec.energy * 0.6, -1, false, contrastRng);

                if (sec.bars > 8)
                {
                    const Line first = L.line;

                    for (auto t : first)
                    {
                        t.onset += 8 * tpb;

                        if (t.onset < len)
                            L.line.push_back (t);
                    }
                }

                break;
            }

            case Kind::Build:
            {
                // The motif's head climbing bar by bar (each rise chosen so the head starts on a chord
                // tone), ending on a held dominant.
                int shift = 0;

                for (int b = 0; b < sec.bars - 1; ++b)
                {
                    if (head.empty())
                        break;

                    const auto& ch = ctx.chordAt (start + b * tpb + head.front().onset + 1);
                    int chosen = shift + (b > 0 ? 1 : 0);

                    for (int up = (b > 0 ? 1 : 0); up <= 3; ++up)
                        if (ch.chord.containsPc (L.sm.pitch (head.front().idx + shift + up)))
                        {
                            chosen = shift + up;
                            break;
                        }

                    shift = chosen;

                    for (auto t : head)
                    {
                        t.onset += b * tpb;
                        t.idx += shift;

                        while (t.idx > thi) t.idx -= n;

                        L.line.push_back (t);
                    }
                }

                // Call and response: a lower instrument echoes each rising head a beat later.
                for (auto t : L.line)
                {
                    t.onset += m.beatTicks;
                    t.idx -= n;

                    if (t.onset + t.dur <= (sec.bars - 1) * tpb)
                        L.imitation.push_back (t);
                }

                const int dominantIdx = n * ((tlo + thi) / 2 / n) + (n >= 7 ? 4 : 3);
                L.line.push_back ({ (sec.bars - 1) * tpb, tpb, dominantIdx, false });
                break;
            }

            case Kind::Outro:
            {
                L.line = augmentLine (motif, 0, 2, std::max (tpb, len - tpb));
                const int tonicIdx = n * ((tlo + thi) / 2 / n);
                L.line.push_back ({ len - tpb, tpb, tonicIdx, false });
                fitLine (L.line, start, ctx, L.sm, tlo, thi);
                break;
            }

            case Kind::Stinger:
            {
                L.line = head;
                const int tonicIdx = n * ((tlo + thi) / 2 / n) + n;
                L.line.push_back ({ std::max (m.beatTicks * 2, len - tpb), tpb, tonicIdx, false });
                fitLine (L.line, start, ctx, L.sm, tlo, thi + n);
                break;
            }
        }

        std::sort (L.line.begin(), L.line.end(), [] (const TNote& a, const TNote& b) { return a.onset < b.onset; });
        L.line.erase (std::remove_if (L.line.begin(), L.line.end(), [&] (const TNote& t) { return t.onset >= len; }), L.line.end());

        for (auto& t : L.line)
            t.dur = std::min (t.dur, len - t.onset);

        resolveLine (L.line, start, ctx, L.sm, L.lo - n, L.hi + n);

        if (! L.imitation.empty())
            resolveLine (L.imitation, start, ctx, L.sm, L.lo - 2 * n, L.hi);
    }

    //==========================================================================
    // Orchestration, section by section.
    Buckets B;
    std::map<std::pair<size_t, int>, int> firstPitch; // (section, track) -> first melody pitch, for runs

    for (size_t si = 0; si < sections.size(); ++si)
    {
        const auto& sec = sections[si];
        const auto& L = lines[si];
        const double i = sec.intensity();
        const int start = sec.startBar * tpb;
        std::set<int> busy;
        Rng secRng (seed ^ (0x5EC7ull * (si + 1)));

        auto free = [&] (Inst inst) -> int
        {
            for (int t : E.of[(int) inst])
                if (busy.count (t) == 0)
                    return t;

            return -1;
        };

        auto melodyOn = [&] (int track, int relativeShift, int* shiftOut, double vel, double legato) -> Notes
        {
            if (track < 0)
                return {};

            busy.insert (track);
            auto notes = placeLine (L.line, start, L.sm, spec.tracks[(size_t) track], ctx, relativeShift, shiftOut, vel, legato);

            if (! notes.empty() && firstPitch.count ({ si, track }) == 0)
                firstPitch[{ si, track }] = notes.front().pitch;

            return notes;
        };

        const double legato = style == MelStyle::Rhythmic ? 0.86 : 0.97;
        Notes leadNotes;

        // 1. Who carries the line.
        std::vector<std::pair<int, int>> owners; // track, octave relation to the lead (in semitones, INT_MIN = own placement)
        int lead = -1;

        // Weighted choice among the free instruments that suit the passage, so every cue
        // (and every variation) hands the tune around differently.
        auto choose = [&] (std::initializer_list<std::pair<Inst, double>> options, bool avoidPrimary) -> int
        {
            std::vector<std::pair<int, double>> cands;
            double total = 0.0;

            for (auto& o : options)
            {
                const int t = free (o.first);

                if (t >= 0 && o.second > 0.0 && ! (avoidPrimary && t == E.primary))
                {
                    cands.push_back ({ t, o.second });
                    total += o.second;
                }
            }

            if (cands.empty())
                return E.primary;

            double r = secRng.uniform (0.0, total);

            for (auto& c : cands)
            {
                r -= c.second;

                if (r <= 0.0)
                    return c.first;
            }

            return cands.back().first;
        };

        const double bright = spec.energy >= 0.6 ? 1.0 : 0.0;

        switch (sec.kind)
        {
            case Kind::Intro:
                lead = choose ({ { Inst::Horn, 3.0 }, { Inst::Vc, 2.0 }, { Inst::Clarinet, 1.5 }, { Inst::Flute, 1.0 }, { Inst::Oboe, 1.0 },
                                 { Inst::Soloist, 1.5 }, { Inst::Vla, 0.5 } }, false);
                break;
            case Kind::Statement:
                lead = E.primary;
                break;
            case Kind::Outro:
                lead = secRng.chance (0.6) ? E.primary
                                           : choose ({ { Inst::Vc, 1.0 }, { Inst::Horn, 1.0 }, { Inst::Oboe, 1.0 }, { Inst::Clarinet, 1.0 } }, false);
                break;
            case Kind::Restatement:
                lead = choose ({ { Inst::Vln1, 4.0 }, { Inst::Horn, 1.5 }, { Inst::Flute, F.lyrical ? 1.0 : 0.3 },
                                 { Inst::Trumpet, bright }, { Inst::Vc, 0.8 } }, true);
                break;
            case Kind::Development:
                lead = choose ({ { Inst::Vln1, 3.0 }, { Inst::Horn, 2.0 }, { Inst::Flute, 1.0 }, { Inst::Clarinet, 1.0 }, { Inst::Vla, 0.5 } }, false);
                break;
            case Kind::Contrast:
                lead = choose ({ { Inst::Oboe, 2.0 }, { Inst::Clarinet, 2.0 }, { Inst::Vc, 2.0 }, { Inst::Flute, 1.5 }, { Inst::Horn, 1.0 },
                                 { Inst::Soloist, 1.5 }, { Inst::Vln1, 0.5 } }, true);
                break;
            case Kind::Build:
                lead = choose ({ { Inst::Horn, 2.0 }, { Inst::Vla, 1.0 }, { Inst::Vc, 1.0 }, { Inst::Trombone, 1.0 }, { Inst::Vln2, 0.5 } }, false);
                break;
            case Kind::Climax:
            case Kind::Stinger:
                lead = choose ({ { Inst::Vln1, 3.0 }, { Inst::Horn, 2.0 }, { Inst::Trumpet, 1.5 * bright } }, false);
                break;
        }

        int leadShift = 0;
        const double melodyVel = sec.kind == Kind::Intro ? 0.85 : 1.0;
        leadNotes = melodyOn (lead, INT_MIN, &leadShift, melodyVel, legato);
        B.add (lead, 0, Notes (leadNotes), Articulation::Legato);

        auto doubleOn = [&] (int track, int octaves)
        {
            if (track < 0 || busy.count (track) > 0)
                return;

            // An octave doubling that would sink into the bass register moves up instead.
            double sum = 0.0;
            int count = 0;

            for (auto& t : L.line)
                if (! t.rest)
                {
                    sum += L.sm.pitch (t.idx) + leadShift + 12 * octaves;
                    ++count;
                }

            int shift = leadShift + 12 * octaves;
            const auto& ts = spec.tracks[(size_t) track];

            while (count > 0 && sum / count + (shift - leadShift - 12 * octaves) < std::max (50, ts.lowNote + 5) && shift < leadShift + 12)
                shift += 12;

            auto notes = melodyOn (track, shift, nullptr, 0.92, legato);
            B.add (track, 0, std::move (notes), Articulation::Legato);
        };

        if (sec.kind == Kind::Statement && i >= 0.5 && classify (spec.tracks[(size_t) E.primary]) != Inst::Vln1)
            doubleOn (free (Inst::Vln1), 0);

        if (sec.kind == Kind::Restatement)
        {
            const Inst pi = classify (spec.tracks[(size_t) E.primary]);

            if (pi == Inst::Flute || pi == Inst::Oboe || pi == Inst::Clarinet || pi == Inst::Piccolo || pi == Inst::Celesta || pi == Inst::Mallets)
                doubleOn (E.primary, 1);
            else if (E.primary != lead)
                doubleOn (E.primary, pi == Inst::Horn || pi == Inst::Vc || pi == Inst::Trombone ? -1 : 0);
        }

        if (sec.kind == Kind::Climax || sec.kind == Kind::Stinger)
        {
            doubleOn (free (Inst::Vln2), -1);
            doubleOn (free (Inst::Horn), -1);

            if (spec.energy >= 0.6)
                doubleOn (free (Inst::Trumpet), -1);

            doubleOn (free (Inst::Flute), 1);

            if (i >= 0.8)
                doubleOn (free (Inst::Piccolo), 2);

            doubleOn (free (Inst::Mallets), 2);

            if (classify (spec.tracks[(size_t) E.primary]) == Inst::Soloist)
                doubleOn (E.primary, 0);
        }

        if (sec.kind == Kind::Restatement && (F.lyrical || F.pastoral))
        {
            doubleOn (free (Inst::Celesta), 1);
            doubleOn (free (Inst::Mallets), 2);
        }

        if (sec.kind == Kind::Build && i >= 0.7)
            doubleOn (free (Inst::Trombone), -1);

        ctx.leadMelody = leadNotes;

        // 2. Counter-line / imitation.
        if ((sec.kind == Kind::Development || sec.kind == Kind::Build) && ! L.imitation.empty())
        {
            int answer = free (Inst::Vc);

            if (answer < 0) answer = free (Inst::Horn);
            if (answer < 0) answer = free (Inst::Vla);

            if (answer >= 0)
            {
                busy.insert (answer);
                auto answerSpec = spec.tracks[(size_t) answer];
                answerSpec.lowNote = std::max (answerSpec.lowNote, ctx.hasBassTrack ? 45 : answerSpec.lowNote);
                auto notes = placeLine (L.imitation, start, L.sm, answerSpec, ctx, INT_MIN, nullptr, 0.9, legato);
                B.add (answer, 0, std::move (notes), Articulation::Legato);
            }
        }

        if (sec.kind == Kind::Restatement || sec.kind == Kind::Contrast || (sec.kind == Kind::Climax && i >= 0.6))
        {
            int counter = -1;

            for (Inst pref : { Inst::Vc, Inst::Horn, Inst::Vla, Inst::Trombone, Inst::Clarinet })
            {
                counter = free (pref);

                if (counter >= 0)
                    break;
            }

            if (counter >= 0)
            {
                busy.insert (counter);
                auto ts = partSpec (spec, counter, sec, Role::CounterMelody, i >= 0.5 ? "flowing" : "sustained", 0.45);

                if (ctx.hasBassTrack && ts.lowNote < 48)
                {
                    ts.lowNote = 48;
                    ts.highNote = std::max (ts.highNote, 67);
                    int instLo = 0, instHi = 127;
                    instrumentRange (ts.program, instLo, instHi);
                    ts.highNote = std::min (ts.highNote, instHi);
                }

                Rng r = secRng.fork (11);
                B.add (counter, 0, counterLine (ctx, sec, ts, L.sm, i >= 0.5, r), Articulation::Legato);
            }
        }

        // 3. Bass.
        {
            std::string pattern = i < 0.4 ? "sustain" : (i < 0.7 ? "root" : (F.drive ? "pedal" : "root"));

            if (sec.kind == Kind::Intro || sec.kind == Kind::Outro || sec.kind == Kind::Build)
                pattern = "sustain";

            const Articulation art = pattern == "sustain" ? Articulation::Sustain : (pattern == "pedal" ? Articulation::Spiccato : Articulation::Marcato);
            int bass = free (Inst::Cb);
            const bool pastoralBassoon = F.pastoral && free (Inst::Bassoon) >= 0 && i < 0.7;

            if (pastoralBassoon)
                bass = free (Inst::Bassoon);

            if (bass < 0) bass = free (Inst::Vc);
            if (bass < 0) bass = free (Inst::Bassoon);
            if (bass < 0) bass = free (Inst::Tuba);

            if (bass >= 0)
            {
                busy.insert (bass);
                const auto ts = partSpec (spec, bass, sec, Role::Bass, pastoralBassoon ? "rootfifth" : pattern, std::max (0.3, i));
                Rng r = secRng.fork (21);
                B.add (bass, 0, generateBass (ctx, ts, r), pastoralBassoon ? Articulation::Staccato : art);
            }

            const int celli = free (Inst::Vc);

            if (celli >= 0 && i >= 0.45 && ! F.drive && ! pastoralBassoon)
            {
                busy.insert (celli);
                auto ts = partSpec (spec, celli, sec, Role::Bass, pattern, std::max (0.3, i));
                ts.lowNote = std::max (ts.lowNote, 43);
                Rng r = secRng.fork (22);
                B.add (celli, 0, generateBass (ctx, ts, r), art);
            }

            const int tuba = free (Inst::Tuba);

            if (tuba >= 0 && i >= 0.8)
            {
                busy.insert (tuba);
                const auto ts = partSpec (spec, tuba, sec, Role::Bass, "sustain", 0.3);
                Rng r = secRng.fork (23);
                B.add (tuba, 0, generateBass (ctx, ts, r), Articulation::Sustain);
            }
        }

        // 4. The rhythmic engine.
        const bool driveSection = F.drive && (sec.kind == Kind::Development || sec.kind == Kind::Build || sec.kind == Kind::Climax
                                              || (sec.kind == Kind::Restatement && i >= 0.55) || sec.kind == Kind::Stinger);

        if (driveSection)
        {
            const double density = i >= 0.8 ? 0.85 : 0.6;

            for (Inst inst : { Inst::Vla, Inst::Vc, Inst::Vln2 })
            {
                const int t = free (inst);

                if (t < 0 || (inst == Inst::Vln2 && i < 0.8))
                    continue;

                busy.insert (t);
                auto ts = partSpec (spec, t, sec, Role::Ostinato, F.ostinato, density);

                // The basses own the bottom octave; the ostinato sits just above them.
                if (ctx.hasBassTrack)
                    ts.lowNote = std::max (ts.lowNote, inst == Inst::Vc ? 43 : (inst == Inst::Vla ? 48 : 55));

                ts.highNote = std::min (ts.highNote, ts.lowNote + 24);
                Rng r = secRng.fork (30 + (int) inst);
                B.add (t, 0, generateOstinato (ctx, ts, r), Articulation::Spiccato);
            }
        }

        if (F.pizzDrive && (sec.kind == Kind::Statement || sec.kind == Kind::Development || sec.kind == Kind::Restatement
                            || (sec.kind == Kind::Contrast && F.pastoral)))
        {
            const int t = E.pizzTrack;

            if (t >= 0 && busy.count (t) == 0)
            {
                busy.insert (t);
                const auto& base = spec.tracks[(size_t) t];
                const bool chordal = base.role == Role::Chords;
                auto ts = partSpec (spec, t, sec, chordal ? Role::Chords : Role::Arpeggio, base.pattern, base.density);

                if (ctx.hasBassTrack)
                {
                    ts.lowNote = std::max (ts.lowNote, 48);
                    ts.highNote = std::max (ts.highNote, ts.lowNote + 19);
                }

                Rng r = secRng.fork (40);
                Notes n;
                Controls unusedCc;

                if (chordal)
                    generateChords (ctx, ts, r, n, unusedCc, false);
                else
                    n = generateArpeggio (ctx, ts, r);

                B.add (t, 0, std::move (n), Articulation::Pizzicato);
            }
        }

        if (E.arpTrack >= 0 && busy.count (E.arpTrack) == 0 && sec.kind != Kind::Build && sec.kind != Kind::Stinger
            && (i < 0.9 || ! F.drive))
        {
            busy.insert (E.arpTrack);
            const auto& base = spec.tracks[(size_t) E.arpTrack];
            auto ts = partSpec (spec, E.arpTrack, sec, Role::Arpeggio, F.arpPattern.empty() ? base.pattern : F.arpPattern,
                                std::min (0.85, base.density + (i - 0.4) * 0.5));

            if (ctx.hasBassTrack)
            {
                ts.lowNote = std::max (ts.lowNote, 48);
                ts.highNote = std::max (ts.highNote, ts.lowNote + 24);
            }

            Rng r = secRng.fork (50);
            B.add (E.arpTrack, 0, generateArpeggio (ctx, ts, r), Articulation::Default);
        }

        // A second colour instrument (celesta, harp, piano) adds a light texture to the quieter sections.
        if (sec.kind == Kind::Intro || sec.kind == Kind::Contrast || sec.kind == Kind::Outro || (sec.kind == Kind::Statement && i < 0.6))
        {
            bool placed = false;

            for (Inst inst : { Inst::Celesta, Inst::Harp, Inst::Piano })
            {
                for (int t : E.of[(int) inst])
                {
                    if (placed || t == E.arpTrack || t == E.primary || busy.count (t) > 0)
                        continue;

                    busy.insert (t);
                    auto ts = partSpec (spec, t, sec, Role::Arpeggio, inst == Inst::Celesta ? "random" : "updown", 0.35);
                    ts.lowNote = std::max (ts.lowNote, 55);
                    ts.highNote = std::max (ts.highNote, ts.lowNote + 19);
                    Rng r = secRng.fork (55 + (int) inst);
                    B.add (t, 0, generateArpeggio (ctx, ts, r), Articulation::Default);
                    placed = true;
                }
            }
        }

        // 5. Harmony, voiced across sections.
        {
            std::vector<int> strings;

            for (Inst inst : { Inst::Vc, Inst::Vla, Inst::Vln2, Inst::Vln1, Inst::Trem })
            {
                const int t = free (inst);

                if (t >= 0 && (inst != Inst::Vc || ! ctx.hasBassTrack || strings.empty()))
                    strings.push_back (t);
            }

            if (! strings.empty() && i >= 0.18)
            {
                const bool tremolo = sec.kind == Kind::Build && (F.dark || F.drive);

                for (int t : strings)
                    busy.insert (t);

                const bool pulse = F.pulseStrings && ! tremolo && (sec.kind == Kind::Development || sec.kind == Kind::Climax);

                choirChords (ctx, sec, spec, strings, pulse ? ChoirRhythm::Pulse : ChoirRhythm::Sustain,
                             tremolo ? Articulation::Tremolo : (pulse ? Articulation::Spiccato : Articulation::Sustain),
                             sec.kind == Kind::Intro ? 0.8 : 0.9, B);
            }

            std::vector<int> winds;

            for (Inst inst : { Inst::Bassoon, Inst::Clarinet, Inst::Oboe, Inst::Flute })
            {
                const int t = free (inst);

                if (t >= 0)
                    winds.push_back (t);
            }

            const bool windChoir = (F.pastoral || F.lyrical || sec.kind == Kind::Climax) && i >= 0.3 && sec.kind != Kind::Intro;

            if (! winds.empty() && windChoir)
            {
                for (int t : winds)
                    busy.insert (t);

                choirChords (ctx, sec, spec, winds, F.pastoral && sec.kind != Kind::Climax ? ChoirRhythm::Pulse : ChoirRhythm::Sustain,
                             F.pastoral ? Articulation::Staccato : Articulation::Sustain, 0.82, B);
            }

            std::vector<int> brass;

            for (Inst inst : { Inst::Trombone, Inst::Horn, Inst::Trumpet })
            {
                for (int t : E.of[(int) inst])
                    if (busy.count (t) == 0)
                        brass.push_back (t);
            }

            if (! brass.empty() && i >= 0.5)
            {
                ChoirRhythm rhythm = ChoirRhythm::Sustain;
                Articulation art = Articulation::Sustain;

                if (i >= 0.8)
                {
                    rhythm = (sec.kind == Kind::Climax && spec.energy >= 0.7 && ! F.dark) ? ChoirRhythm::Heroic : ChoirRhythm::Hits;
                    art = Articulation::Marcato;
                }

                // Horns alone hold the harmony at moderate intensity; the full brass enters for the climax.
                std::vector<int> group;

                for (int t : brass)
                    if (i >= 0.75 || classify (spec.tracks[(size_t) t]) == Inst::Horn)
                        group.push_back (t);

                for (int t : group)
                    busy.insert (t);

                choirChords (ctx, sec, spec, group, rhythm, art, rhythm == ChoirRhythm::Sustain ? 0.78 : 0.95, B);
            }

            const int choir = free (Inst::Choir);

            if (choir >= 0 && i >= 0.55)
            {
                busy.insert (choir);
                choirChords (ctx, sec, spec, { choir }, ChoirRhythm::Sustain, Articulation::Sustain, 0.85, B);
            }
        }

        // 6. Timpani and percussion.
        for (int t : E.of[(int) Inst::Timpani])
            timpaniPart (ctx, sec, spec.tracks[(size_t) t], t, B);

        for (int t : E.of[(int) Inst::Perc])
        {
            Rng r = secRng.fork (70 + t);
            percussionPart (ctx, sec, spec.tracks[(size_t) t], t, F, r, B);
        }
    }

    //==========================================================================
    // Transitions: swells into hits, runs and glissandi into new sections.
    for (size_t si = 1; si < sections.size(); ++si)
    {
        const auto& sec = sections[si];
        const int tick = sec.startBar * tpb;

        if (sec.hit && F.hits)
            tuttiHit (ctx, sec, spec, E, F, B);

        if (! sec.run)
            continue;

        const int noteTicks = spec.tempo <= 110 ? kStep / 2 : kStep;
        const int runLength = m.beatTicks;

        for (Inst inst : { Inst::Vln1, Inst::Flute })
        {
            const int t = E.first (inst);

            if (t < 0)
                continue;

            const auto& ts = spec.tracks[(size_t) t];
            auto it = firstPitch.find ({ si, t });
            int target = it != firstPitch.end() ? it->second : ts.lowNote + (ts.highNote - ts.lowNote) * 2 / 3;
            target = std::max (ts.lowNote + 7, std::min (ts.highNote, target));

            // Leave room: drop any notes of this part in the run's last beat.
            auto& dst = B.notes[{ t, 0 }];
            dst.erase (std::remove_if (dst.begin(), dst.end(), [&] (const RawNote& n)
                       { return n.start >= tick - runLength && n.start < tick; }), dst.end());

            for (auto& n : dst)
                if (n.start < tick - runLength && n.start + n.length > tick - runLength)
                    n.length = std::max (kStep / 2, tick - runLength - n.start);

            B.add (t, 0, scaleRun (target, sec, tick, runLength, noteTicks, ts.velocity * ctx.energyAt (tick - 1), ts.lowNote), Articulation::Legato);
        }

        if (E.any (Inst::Harp) && (F.lyrical || sec.kind == Kind::Contrast))
        {
            // A glissando with the pedals set to the sounding chord, landing on the new downbeat.
            const int t = E.first (Inst::Harp);
            const auto& ts = spec.tracks[(size_t) t];
            const auto& before = ctx.chordAt (tick - 1);
            const auto& after = ctx.chordAt (tick);
            const int top = std::min (ts.highNote, ts.lowNote + 36);
            const int glissTicks = m.beatTicks * 2;
            const int noteTicks = kStep / 2;
            const int count = glissTicks / noteTicks;
            std::vector<int> pitches;

            for (int p = top - 1; p >= ts.lowNote && (int) pitches.size() < count; --p)
                if (before.chord.containsPc (p))
                    pitches.push_back (p);

            std::reverse (pitches.begin(), pitches.end());
            Notes n;
            int at = tick - (int) pitches.size() * noteTicks;

            for (int p : pitches)
            {
                RawNote r;
                r.start = at;
                r.length = noteTicks;
                r.pitch = p;
                r.velocity = ts.velocity * (0.55 + 0.35 * (double) (at - (tick - glissTicks)) / glissTicks);
                n.push_back (r);
                at += noteTicks;
            }

            int land = top;

            while (! after.chord.containsPc (land) && land > ts.lowNote)
                --land;

            RawNote r;
            r.start = tick;
            r.length = m.beatTicks;
            r.pitch = land;
            r.velocity = ts.velocity * 0.95;
            n.push_back (r);
            B.add (t, 0, std::move (n), Articulation::Default);
        }
    }

    //==========================================================================
    // Assemble the output tracks (pizzicato and tremolo get their own).
    Composition comp;
    comp.title = spec.title;
    comp.tempo = spec.tempo;
    comp.tsNum = spec.tsNum;
    comp.tsDen = spec.tsDen;
    comp.bars = bars;
    comp.ticksPerBar = m.ticksPerBar;
    comp.lengthTicks = ctx.totalTicks;
    comp.keyRoot = spec.keyRoot;
    comp.endKeyRoot = sections.back().keyRoot;
    comp.mode = spec.mode;
    comp.progression = spec.progression;

    for (auto& c : ctx.chords)
    {
        // Spell each chord in its own section's key (a Db major contrast section uses flats).
        Mode mode = spec.mode;

        for (auto& sec : sections)
            if (c.start >= sec.startBar * m.ticksPerBar && c.start < (sec.startBar + sec.bars) * m.ticksPerBar)
                mode = sec.mode;

        comp.chords.push_back ({ c.start, c.end, chordName (c.chord, keyPrefersFlats (c.keyRoot, mode)), c.roman, c.pcs, c.bassPc });
    }

    std::set<int> usedChannels;

    for (auto& t : spec.tracks)
        usedChannels.insert (t.channel);

    auto nextChannel = [&] (int program) -> int
    {
        for (int ch = 0; ch < 16; ++ch)
            if (ch != 9 && usedChannels.count (ch) == 0)
            {
                usedChannels.insert (ch);
                return ch;
            }

        // Out of channels: share one with a part that has the same program.
        for (size_t i = 0; i < comp.tracks.size(); ++i)
            if (comp.tracks[i].program == program && ! comp.tracks[i].isDrum)
                return comp.tracks[i].channel;

        return 15;
    };

    for (int ti = 0; ti < (int) spec.tracks.size(); ++ti)
    {
        int baseIndex = -1;

        for (int group = 0; group < 3; ++group)
        {
            auto it = B.notes.find ({ ti, group });
            const bool empty = it == B.notes.end() || it->second.empty();

            if (group != 0 && empty)
                continue;

            TrackSpec ts = spec.tracks[(size_t) ti];
            Notes notes = it != B.notes.end() ? it->second : Notes {};

            if (group == 1)
            {
                ts.name += " (pizz.)";
                ts.program = 45;
                ts.channel = nextChannel (45);
            }
            else if (group == 2)
            {
                ts.name += " (trem.)";
                ts.program = 44;
                ts.channel = nextChannel (44);
            }

            // Silent parts are kept too, so the DAW template stays complete.
            Controls cc;

            if (! notes.empty())
                addExpression (ctx, ts, notes, cc);

            Rng post (seed ^ (0xABCDEFull + (uint64_t) ti * 31 + (uint64_t) group));
            comp.tracks.push_back (finalizeTrack (ctx, ts, notes, cc, post));

            if (group == 0)
                baseIndex = (int) comp.tracks.size() - 1;
            else
                comp.tracks.back().splitFrom = baseIndex;
        }
    }

    //==========================================================================
    // Markers, loop points, form summary.
    std::string form;

    for (auto& sec : sections)
    {
        comp.markers.push_back ({ sec.startBar * tpb, kindLabel (sec.kind) });

        if (! form.empty())
            form += " \xE2\x80\x93 ";

        form += kindLabel (sec.kind);
    }

    if (loopable)
    {
        int loopStart = 0, loopEnd = ctx.totalTicks;

        for (auto& sec : sections)
            if (sec.kind == Kind::Statement)
            {
                loopStart = sec.startBar * tpb;
                break;
            }

        for (auto& sec : sections)
            if (sec.kind == Kind::Stinger)
                loopEnd = sec.startBar * tpb;

        comp.loopStart = loopStart;
        comp.loopEnd = loopEnd;
        comp.markers.push_back ({ loopStart, "Loop Start" });
        comp.markers.push_back ({ loopEnd, "Loop End" });
        std::sort (comp.markers.begin(), comp.markers.end(), [] (const Marker& a, const Marker& b) { return a.tick < b.tick; });
    }

    comp.form = form;
    return comp;
}

} // namespace pm
