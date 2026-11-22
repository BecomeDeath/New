#include "RequestCheck.h"
#include "PromptInterpreter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>

namespace pm
{

namespace
{
    std::string lower (std::string s)
    {
        for (auto& c : s)
            c = (char) std::tolower ((unsigned char) c);

        return s;
    }

    bool contains (const std::string& haystack, const std::string& needle)
    {
        return ! needle.empty() && haystack.find (needle) != std::string::npos;
    }

    bool minorFamily (Mode m)
    {
        switch (m)
        {
            case Mode::Minor: case Mode::HarmonicMinor: case Mode::MelodicMinor: case Mode::PentatonicMinor:
            case Mode::Dorian: case Mode::Phrygian: case Mode::Locrian: case Mode::Blues: case Mode::Japanese:
                return true;
            default:
                return false;
        }
    }

    std::string joinNames (const std::vector<InstrumentNeed>& needs)
    {
        std::string out;

        for (size_t i = 0; i < needs.size(); ++i)
        {
            if (i > 0)
                out += i + 1 == needs.size() ? " and " : ", ";

            out += lower (needs[i].name);
        }

        return out;
    }

    std::string timeText (double seconds)
    {
        const int s = (int) std::lround (seconds);

        if (s < 90)
            return std::to_string (s) + " seconds";

        return std::to_string (s / 60) + ":" + (s % 60 < 10 ? "0" : "") + std::to_string (s % 60);
    }

    const CatalogueEntry* catalogueEntry (const InstrumentNeed& n)
    {
        for (auto& e : instrumentCatalogue())
            if (lower (e.name) == lower (n.name))
                return &e;

        for (auto& e : instrumentCatalogue())
            if (e.program == n.program && ! n.percussion)
                return &e;

        return nullptr;
    }

    bool trackMatches (const TrackSpec& t, const InstrumentNeed& n)
    {
        return trackIs (t.name, t.program, t.role == Role::Percussion, n);
    }

    void addTrack (StyleSpec& spec, const InstrumentNeed& n)
    {
        TrackSpec t;
        t.name = n.name;
        t.program = std::max (0, n.program);
        t.density = 0.45;
        t.velocity = 85;
        t.pattern = "auto";

        if (n.percussion)
        {
            t.role = Role::Percussion;
            t.lowNote = 35;
            t.highNote = 81;
        }
        else if (const auto* e = catalogueEntry (n))
        {
            t.role = e->role == Role::Percussion ? Role::CounterMelody : e->role;
            t.lowNote = e->low;
            t.highNote = e->high;
        }
        else
        {
            t.role = Role::CounterMelody;
            instrumentRange (t.program, t.lowNote, t.highNote);
        }

        if (n.lead)
            t.role = Role::Melody;

        // Only one part plays the tune.
        if (t.role == Role::Melody)
            for (auto& o : spec.tracks)
                if (o.role == Role::Melody)
                    o.role = Role::CounterMelody;

        spec.tracks.push_back (t);
    }

    /** "Piano Comping" -> "Piano", "Harp (Accompaniment)" -> "Harp" (keeping the case). */
    std::string baseName (const std::string& name)
    {
        std::string out = name.substr (0, name.find (" ("));
        static const char* words[] = { " Comping", " Accompaniment", " Chords", " Melody", " Lead" };

        for (auto* w : words)
        {
            const std::string suffix (w);

            if (out.size() > suffix.size() && out.compare (out.size() - suffix.size(), suffix.size(), suffix) == 0)
                out = out.substr (0, out.size() - suffix.size());
        }

        return out;
    }

    /** "Harp plays", "Drums play", "Choir sings". */
    std::string plays (const std::string& lowerName)
    {
        const bool voice = contains (lowerName, "choir") || contains (lowerName, "voice") || contains (lowerName, "vocal");
        const bool plural = lowerName.size() > 2 && lowerName.back() == 's' && lowerName[lowerName.size() - 2] != 's';
        return voice ? (plural ? " sing" : " sings") : (plural ? " play" : " plays");
    }

    bool isPolyphonic (int p)
    {
        return (p >= 0 && p <= 21) || (p >= 24 && p <= 31) || p == 46 || p == 105 || p == 107 || p == 108;
    }

    /** Gives the tune to a track; a keyboard or guitar that only had chords plays both. */
    void giveLead (StyleSpec& spec, size_t index)
    {
        for (auto& o : spec.tracks)
            if (o.role == Role::Melody)
                o.role = Role::CounterMelody;

        auto& t = spec.tracks[index];

        if (isPolyphonic (t.program) && (t.role == Role::Chords || t.role == Role::Arpeggio))
        {
            TrackSpec lead = t;
            lead.name = baseName (t.name) + " (Melody)";
            lead.role = Role::Melody;
            lead.lowNote = std::max (t.lowNote, 60);
            lead.highNote = std::max (lead.lowNote + 19, t.highNote);
            lead.pattern = "lyrical";
            lead.density = 0.45;
            lead.velocity = std::max (t.velocity, 85);
            t.highNote = std::min (t.highNote, 67);
            spec.tracks.insert (spec.tracks.begin() + (long) index, lead);
            return;
        }

        t.role = Role::Melody;
        t.pattern = "auto";
        t.lowNote = std::max (t.lowNote, std::min (t.highNote - 19, 55));
    }

    /** Which players of a section to keep first when it's cut down to a trio or quartet: the
        tune, then the classic chamber line-up (violin, cello, viola; flute, clarinet, oboe...). */
    int sectionPriority (const TrackSpec& t)
    {
        const auto n = lower (t.name);

        if (t.role == Role::Melody)                                   return 70;
        if (contains (n, "violins ii") || contains (n, "violin ii"))  return 55;
        if (contains (n, "cell"))                                     return 66;
        if (contains (n, "viola"))                                    return 62;
        if (contains (n, "clarinet"))                                 return 66;
        if (contains (n, "horn"))                                     return 64;
        if (contains (n, "oboe"))                                     return 63;
        if (contains (n, "bassoon"))                                  return 61;
        if (contains (n, "trombone"))                                 return 60;
        if (contains (n, "tuba"))                                     return 58;
        if (contains (n, "bass"))                                     return 45;
        return t.role == Role::Bass ? 50 : (t.role == Role::CounterMelody ? 40 : 35);
    }

    void trimEnsemble (StyleSpec& spec, const Request& req)
    {
        auto groups = [&]
        {
            std::vector<std::string> keys;

            for (auto& t : spec.tracks)
            {
                const auto k = instrumentKey (t.name, t.program, t.role == Role::Percussion);

                if (std::find (keys.begin(), keys.end(), k) == keys.end())
                    keys.push_back (k);
            }

            return keys;
        };

        for (int guard = 0; guard < 32; ++guard)
        {
            const auto keys = groups();

            if ((int) keys.size() <= req.ensembleSize)
                break;

            // The least essential player goes: never one that was asked for; extra melody
            // instruments before the rhythm section.
            int worst = -1, worstScore = 1 << 30;

            for (int g = 0; g < (int) keys.size(); ++g)
            {
                int score = 0;

                for (auto& t : spec.tracks)
                {
                    if (instrumentKey (t.name, t.program, t.role == Role::Percussion) != keys[(size_t) g])
                        continue;

                    // An instrument that was asked for stays, unless another player covers the same request
                    // ("string trio" from a five-part string section).
                    for (auto& n : req.instruments)
                    {
                        if (! trackMatches (t, n))
                            continue;

                        const bool others = std::any_of (spec.tracks.begin(), spec.tracks.end(), [&] (const TrackSpec& o)
                        {
                            return trackMatches (o, n) && instrumentKey (o.name, o.program, o.role == Role::Percussion) != keys[(size_t) g];
                        });

                        score = std::max (score, others ? 200 + sectionPriority (t) : 1000);
                    }

                    score = std::max (score, t.role == Role::Bass ? 60 : t.role == Role::Percussion ? 50
                                           : (t.role == Role::Chords || t.role == Role::Arpeggio || t.role == Role::Pad) ? 30
                                           : t.role == Role::Melody ? 20 : 10);
                }

                if (score < worstScore)
                {
                    worstScore = score;
                    worst = g;
                }
            }

            if (worst < 0 || worstScore >= 1000)
                break;

            bool hadMelody = false;
            const auto key = keys[(size_t) worst];

            spec.tracks.erase (std::remove_if (spec.tracks.begin(), spec.tracks.end(), [&] (const TrackSpec& t)
            {
                const bool gone = instrumentKey (t.name, t.program, t.role == Role::Percussion) == key;
                hadMelody = hadMelody || (gone && t.role == Role::Melody);
                return gone;
            }), spec.tracks.end());

            if (hadMelody && std::none_of (spec.tracks.begin(), spec.tracks.end(), [] (const TrackSpec& t) { return t.role == Role::Melody; }))
            {
                // The tune moves to an instrument that was asked for, else the highest remaining part.
                int to = -1;

                for (size_t i = 0; i < spec.tracks.size() && to < 0; ++i)
                    for (auto& n : req.instruments)
                        if (! n.percussion && trackMatches (spec.tracks[i], n))
                            to = (int) i;

                for (size_t i = 0; i < spec.tracks.size() && to < 0; ++i)
                    if (spec.tracks[i].role != Role::Percussion && spec.tracks[i].role != Role::Bass)
                        to = (int) i;

                if (to >= 0)
                    giveLead (spec, (size_t) to);
            }
        }
    }
}

//==============================================================================
bool trackIs (const std::string& trackName, int program, bool percussion, const InstrumentNeed& n)
{
    if (n.percussion && percussion)
        return true;

    if (percussion)
        return false;

    for (auto& r : n.programs)
        if (program >= r.first && program <= r.second)
            return true;

    const auto name = lower (trackName);

    for (auto& w : n.words)
        if (contains (name, lower (w)))
        {
            // "bass" isn't "bassoon", "guitar" isn't "bass guitar"
            const auto lw = lower (w);

            if (lw == "bass" && (contains (name, "bassoon") || contains (name, "bass drum") || contains (name, "bass clarinet")))
                continue;

            if (lw == "guitar" && contains (name, "bass guitar"))
                continue;

            if (lw == "horn" && contains (name, "english horn"))
                continue;

            return true;
        }

    return false;
}

std::string instrumentKey (const std::string& trackName, int program, bool percussion)
{
    if (percussion)
        return "percussion";

    auto name = lower (trackName);
    const auto paren = name.find (" (");

    if (paren != std::string::npos)
        name = name.substr (0, paren);

    static const char* roleWords[] = { " comping", " accompaniment", " melody", " chords", " lead", " right hand", " left hand", " rh", " lh", " arpeggio" };

    for (bool changed = true; changed;)
    {
        changed = false;

        for (auto* w : roleWords)
        {
            const std::string suffix (w);

            if (name.size() > suffix.size() && name.compare (name.size() - suffix.size(), suffix.size(), suffix) == 0)
            {
                name = name.substr (0, name.size() - suffix.size());
                changed = true;
            }
        }
    }

    return name.empty() ? std::to_string (program) : name;
}

//==============================================================================
void applyRequest (StyleSpec& spec, const Request& req)
{
    if (req.keyRoot >= 0)
        spec.keyRoot = wrapPc (req.keyRoot);

    if (req.mode >= 0)
        spec.mode = (Mode) req.mode;

    if (req.tempo > 0)
        spec.tempo = req.tempo;
    else
    {
        if (req.tempoMax > 0 && spec.tempo > req.tempoMax)  spec.tempo = req.tempoMax;
        if (req.tempoMin > 0 && spec.tempo < req.tempoMin)  spec.tempo = req.tempoMin;
    }

    if (req.tsNum > 0 && req.tsDen > 0)
    {
        spec.tsNum = req.tsNum;
        spec.tsDen = req.tsDen;
    }

    const double quarters = spec.tsNum * 4.0 / std::max (1, spec.tsDen);

    if (req.bars > 0)
    {
        spec.bars = req.bars;

        if (spec.orchestral)
            spec.length = "bars";
    }
    else if (req.seconds > 0.0)
    {
        // Whole phrases, with the tempo nudged (by up to ~15%) so the piece lands on time.
        int bestBars = spec.bars, bestTempo = spec.tempo;
        double bestCost = 1e18;

        for (int bars = 2; bars <= 160; ++bars)
        {
            int tempo = req.tempo > 0 ? req.tempo : (int) std::lround (bars * quarters * 60.0 / req.seconds);

            if (req.tempo <= 0)
                tempo = std::max ((int) (spec.tempo * 0.85), std::min ((int) std::ceil (spec.tempo * 1.15), tempo));

            const double secs = bars * quarters * 60.0 / std::max (1, tempo);
            const double phrase = bars % 4 == 0 ? 0.0 : (bars % 2 == 0 ? 1.0 : 3.0);
            const double cost = std::abs (secs - req.seconds) + phrase + std::abs (tempo - spec.tempo) * 0.03;

            if (cost < bestCost)
            {
                bestCost = cost;
                bestBars = bars;
                bestTempo = tempo;
            }
        }

        spec.bars = bestBars;
        spec.tempo = std::max (40, std::min (220, bestTempo));

        if (spec.orchestral)
            spec.length = "bars";
    }
    else if (! spec.orchestral && (req.maxSeconds > 0.0 || req.minSeconds > 0.0))
    {
        const double barSeconds = quarters * 60.0 / std::max (1, spec.tempo);

        if (req.maxSeconds > 0.0 && spec.bars * barSeconds > req.maxSeconds)
            spec.bars = std::max (4, (int) std::floor (req.maxSeconds / barSeconds / 4.0) * 4);

        if (req.minSeconds > 0.0 && spec.bars * barSeconds < req.minSeconds)
            spec.bars = std::min (128, (int) std::ceil (req.minSeconds / barSeconds / 4.0) * 4);
    }

    // Instruments left out.
    for (auto& ex : req.excluded)
        spec.tracks.erase (std::remove_if (spec.tracks.begin(), spec.tracks.end(), [&] (const TrackSpec& t) { return trackMatches (t, ex); }),
                           spec.tracks.end());

    if (req.onlyThese && ! req.instruments.empty())
    {
        // Nothing but what was asked for: keep a plan that already is exactly that, otherwise
        // arrange the piece for those instruments.
        bool fits = ! spec.tracks.empty();

        for (auto& t : spec.tracks)
            fits = fits && std::any_of (req.instruments.begin(), req.instruments.end(), [&] (const InstrumentNeed& n) { return trackMatches (t, n); });

        for (auto& n : req.instruments)
            fits = fits && std::any_of (spec.tracks.begin(), spec.tracks.end(), [&] (const TrackSpec& t) { return trackMatches (t, n); });

        if (! fits)
        {
            std::vector<InstrumentChoice> choices;

            for (auto& n : req.instruments)
            {
                InstrumentChoice c;
                c.name = n.name;
                c.program = n.percussion ? -1 : n.program;
                c.role = n.lead ? "melody" : "auto";
                choices.push_back (c);
            }

            applyInstrumentChoices (spec, choices);
        }
    }
    else
    {
        for (auto& n : req.instruments)
            if (std::none_of (spec.tracks.begin(), spec.tracks.end(), [&] (const TrackSpec& t) { return trackMatches (t, n); }))
                addTrack (spec, n);
    }

    // The instrument asked to lead gets the tune.
    for (auto& n : req.instruments)
    {
        if (! n.lead || n.percussion)
            continue;

        bool leads = false;
        int first = -1;

        for (size_t i = 0; i < spec.tracks.size(); ++i)
        {
            if (! trackMatches (spec.tracks[i], n))
                continue;

            leads = leads || spec.tracks[i].role == Role::Melody;

            if (first < 0)
                first = (int) i;
        }

        if (! leads && first >= 0)
            giveLead (spec, (size_t) first);

        break;
    }

    if (req.ensembleSize > 0)
        trimEnsemble (spec, req);
}

//==============================================================================
RequestReport checkRequest (const Composition& comp, const Request& req)
{
    RequestReport report;
    auto add = [&] (const std::string& label, bool met, const std::string& detail = {})
    {
        report.criteria.push_back ({ label, met, met ? std::string() : detail });
    };

    struct Part { const Track* t; int notes; };
    std::vector<Part> parts;

    for (auto& t : comp.tracks)
        if (! t.notes.empty())
            parts.push_back ({ &t, (int) t.notes.size() });

    auto matches = [] (const Track& t, const InstrumentNeed& n) { return trackIs (t.name, t.program, t.isDrum, n); };

    // Instruments
    if (req.onlyThese && ! req.instruments.empty())
    {
        std::vector<std::string> extra;

        for (auto& p : parts)
            if (std::none_of (req.instruments.begin(), req.instruments.end(), [&] (const InstrumentNeed& n) { return matches (*p.t, n); }))
                extra.push_back (p.t->name);

        std::string detail = "also has ";

        for (size_t i = 0; i < extra.size(); ++i)
            detail += (i > 0 ? ", " : "") + extra[i];

        add ("Only " + joinNames (req.instruments), extra.empty(), detail);

        // ...and each of them plays.
        std::string silent;

        for (auto& t : comp.tracks)
            if (t.splitFrom < 0 && t.notes.size() < 4)
                silent += (silent.empty() ? "" : ", ") + t.name;

        add ("Every instrument plays", silent.empty(), silent + " barely play");
    }

    for (auto& n : req.instruments)
    {
        int notes = 0, leadNotes = 0;

        for (auto& p : parts)
            if (matches (*p.t, n))
            {
                notes += p.notes;
                leadNotes += p.t->role == Role::Melody ? p.notes : 0;
            }

        const std::string name = lower (n.name);
        const std::string capital = std::string (1, (char) std::toupper ((unsigned char) name[0])) + name.substr (1);
        add (capital + plays (name), notes >= 6, notes == 0 ? "no " + name + " part" : "only " + std::to_string (notes) + " notes");

        if (n.lead)
            add (capital + plays (name) + " the melody", leadNotes >= 6, "the melody is on another instrument");
    }

    for (auto& ex : req.excluded)
    {
        std::string found;

        for (auto& p : parts)
            if (matches (*p.t, ex))
                found += (found.empty() ? "" : ", ") + p.t->name;

        add ("No " + lower (ex.name), found.empty(), "has " + found);
    }

    if (req.ensembleSize > 0)
    {
        std::set<std::string> players;

        for (auto& p : parts)
            players.insert (instrumentKey (p.t->name, p.t->program, p.t->isDrum));

        static const char* names[] = { "", "A solo", "A duo", "A trio", "A quartet", "A quintet", "A sextet" };
        const std::string label = req.ensembleSize <= 6 ? std::string (names[req.ensembleSize]) : std::to_string (req.ensembleSize) + " players";
        add (label + " (" + std::to_string (req.ensembleSize) + " instruments)", (int) players.size() == req.ensembleSize,
             std::to_string (players.size()) + " instruments");
    }

    // Key and scale
    if (req.keyRoot >= 0 || req.mode >= 0)
    {
        bool met = true;
        std::string label;

        if (req.keyRoot >= 0 && req.mode >= 0)
        {
            const auto wanted = (Mode) req.mode;
            const bool exact = wanted != Mode::Major && wanted != Mode::Minor;
            label = keyName (req.keyRoot, wanted);
            met = comp.keyRoot == wrapPc (req.keyRoot) && (exact ? comp.mode == wanted : minorFamily (comp.mode) == minorFamily (wanted));
        }
        else if (req.keyRoot >= 0)
        {
            label = "Key of " + pitchClassName (req.keyRoot, keyPrefersFlats (req.keyRoot, comp.mode));
            met = comp.keyRoot == wrapPc (req.keyRoot);
        }
        else
        {
            label = std::string (modeInfo ((Mode) req.mode).displayName) + " scale";
            met = (Mode) req.mode == Mode::Major || (Mode) req.mode == Mode::Minor ? minorFamily (comp.mode) == minorFamily ((Mode) req.mode)
                                                                                  : comp.mode == (Mode) req.mode;
        }

        add (label, met, "it's in " + keyName (comp.keyRoot, comp.mode));
    }
    else if (req.modeFeel != 0)
    {
        const bool wantMinor = req.modeFeel < 0;
        add (wantMinor ? "A minor key, for the mood" : "A major key, for the mood", minorFamily (comp.mode) == wantMinor,
             "it's in " + keyName (comp.keyRoot, comp.mode));
    }

    // Tempo
    if (req.tempo > 0)
        add (std::to_string (req.tempo) + " BPM", std::abs (comp.tempo - req.tempo) <= 1, std::to_string (comp.tempo) + " BPM");

    if (req.tempo <= 0 && req.tempoMax > 0)
        add ("Slow (up to " + std::to_string (req.tempoMax) + " BPM)", comp.tempo <= req.tempoMax, std::to_string (comp.tempo) + " BPM");

    if (req.tempo <= 0 && req.tempoMin > 0)
        add ("Fast (" + std::to_string (req.tempoMin) + " BPM or more)", comp.tempo >= req.tempoMin, std::to_string (comp.tempo) + " BPM");

    // Length
    const double secs = comp.seconds();

    if (req.bars > 0)
        add (std::to_string (req.bars) + " bars", comp.bars == req.bars, std::to_string (comp.bars) + " bars");
    else if (req.seconds > 0.0)
        add ("About " + timeText (req.seconds), std::abs (secs - req.seconds) <= std::max (3.0, req.seconds * 0.12), timeText (secs));
    else
    {
        if (req.maxSeconds > 0.0 && req.minSeconds > 0.0)
            add ("Between " + timeText (req.minSeconds) + " and " + timeText (req.maxSeconds), secs >= req.minSeconds && secs <= req.maxSeconds, timeText (secs));
        else if (req.maxSeconds > 0.0)
            add ("Short (up to " + timeText (req.maxSeconds) + ")", secs <= req.maxSeconds, timeText (secs));
        else if (req.minSeconds > 0.0)
            add ("Long (at least " + timeText (req.minSeconds) + ")", secs >= req.minSeconds, timeText (secs));
    }

    if (req.tsNum > 0)
        add (std::to_string (req.tsNum) + "/" + std::to_string (req.tsDen) + " time", comp.tsNum == req.tsNum && comp.tsDen == req.tsDen,
             std::to_string (comp.tsNum) + "/" + std::to_string (comp.tsDen));

    // Dynamics: the average loudness of the pitched parts.
    if (req.loudness != 0)
    {
        double sum = 0.0;
        int count = 0;

        for (auto& p : parts)
            if (! p.t->isDrum)
                for (auto& n : p.t->notes)
                {
                    sum += n.velocity;
                    ++count;
                }

        const double mean = count > 0 ? sum / count : 0.0;
        const bool soft = req.loudness < 0;
        add (soft ? "Soft dynamics" : "Strong, loud dynamics", soft ? mean <= 84.0 : mean >= 78.0,
             "average velocity " + std::to_string ((int) std::lround (mean)));
    }

    return report;
}

} // namespace pm
