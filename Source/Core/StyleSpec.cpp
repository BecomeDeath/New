#include "StyleSpec.h"

#include <algorithm>
#include <cctype>
#include <cmath>
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

    template <typename T>
    T clampValue (T v, T lo, T hi) { return std::max (lo, std::min (hi, v)); }
}

const std::vector<Role>& allRoles()
{
    static const std::vector<Role> roles { Role::Melody, Role::CounterMelody, Role::Chords, Role::Pad,
                                           Role::Arpeggio, Role::Bass, Role::Ostinato, Role::Percussion };
    return roles;
}

const char* roleId (Role r)
{
    switch (r)
    {
        case Role::Melody:        return "melody";
        case Role::CounterMelody: return "countermelody";
        case Role::Chords:        return "chords";
        case Role::Pad:           return "pad";
        case Role::Arpeggio:      return "arpeggio";
        case Role::Bass:          return "bass";
        case Role::Ostinato:      return "ostinato";
        case Role::Percussion:    return "percussion";
    }

    return "melody";
}

const char* roleDisplayName (Role r)
{
    switch (r)
    {
        case Role::Melody:        return "Melody";
        case Role::CounterMelody: return "Counter-melody";
        case Role::Chords:        return "Chords";
        case Role::Pad:           return "Pad";
        case Role::Arpeggio:      return "Arpeggio";
        case Role::Bass:          return "Bass";
        case Role::Ostinato:      return "Ostinato";
        case Role::Percussion:    return "Percussion";
    }

    return "Melody";
}

bool roleFromString (const std::string& text, Role& out)
{
    const auto t = lower (text);

    for (auto r : allRoles())
    {
        if (t == roleId (r))
        {
            out = r;
            return true;
        }
    }

    if (t == "counter" || t == "counter_melody" || t == "counter-melody") { out = Role::CounterMelody; return true; }
    if (t == "drums" || t == "drum" || t == "perc")                       { out = Role::Percussion;    return true; }
    if (t == "lead")                                                       { out = Role::Melody;        return true; }
    if (t == "harmony" || t == "accompaniment")                            { out = Role::Chords;        return true; }
    if (t == "arp")                                                        { out = Role::Arpeggio;      return true; }

    return false;
}

const char* dynamicsId (Dynamics d)
{
    switch (d)
    {
        case Dynamics::Flat:  return "flat";
        case Dynamics::Build: return "build";
        case Dynamics::Arc:   return "arc";
        case Dynamics::Fade:  return "fade";
    }

    return "arc";
}

bool dynamicsFromString (const std::string& text, Dynamics& out)
{
    const auto t = lower (text);

    for (auto d : { Dynamics::Flat, Dynamics::Build, Dynamics::Arc, Dynamics::Fade })
    {
        if (t == dynamicsId (d))
        {
            out = d;
            return true;
        }
    }

    return false;
}

const std::vector<std::string>& patternsForRole (Role r)
{
    static const std::vector<std::string> melody     { "auto", "lyrical", "rhythmic", "sparse" };
    static const std::vector<std::string> counter    { "auto", "sustained", "flowing", "double" };
    static const std::vector<std::string> chords     { "auto", "block", "sustain", "pulse", "broken", "alberti", "strum",
                                                       "stabs", "offbeat", "comp", "waltz", "power", "bossa" };
    static const std::vector<std::string> pad        { "auto", "sustain", "swell" };
    static const std::vector<std::string> arpeggio   { "auto", "up", "down", "updown", "random" };
    static const std::vector<std::string> bass       { "auto", "sustain", "root", "rootfifth", "octave", "offbeat", "pedal",
                                                       "walking", "syncopated", "808", "timpani", "boogie", "bossa" };
    static const std::vector<std::string> ostinato   { "auto", "epic", "pulse", "gallop" };
    static const std::vector<std::string> percussion { "auto", "rock", "pop", "four_on_floor", "hiphop", "trap", "lofi", "jazz",
                                                       "bossa", "orchestral", "march", "waltz", "ambient", "breakbeat", "funk",
                                                       "shuffle", "metal", "folk", "tribal", "arabic", "reggae", "chiptune",
                                                       "halftime", "synthwave" };

    switch (r)
    {
        case Role::Melody:        return melody;
        case Role::CounterMelody: return counter;
        case Role::Chords:        return chords;
        case Role::Pad:           return pad;
        case Role::Arpeggio:      return arpeggio;
        case Role::Bass:          return bass;
        case Role::Ostinato:      return ostinato;
        case Role::Percussion:    return percussion;
    }

    return melody;
}

const std::vector<std::string>& allPatternIds()
{
    static const std::vector<std::string> ids = []
    {
        std::vector<std::string> v;
        std::set<std::string> seen;

        for (auto r : allRoles())
            for (auto& p : patternsForRole (r))
                if (seen.insert (p).second)
                    v.push_back (p);

        return v;
    }();

    return ids;
}

const char* gmProgramName (int program)
{
    static const char* names[128] = {
        "Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano", "Honky-tonk Piano",
        "Electric Piano 1", "Electric Piano 2", "Harpsichord", "Clavinet",
        "Celesta", "Glockenspiel", "Music Box", "Vibraphone", "Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
        "Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ", "Reed Organ", "Accordion", "Harmonica", "Tango Accordion",
        "Nylon Guitar", "Steel Guitar", "Jazz Guitar", "Clean Electric Guitar",
        "Muted Guitar", "Overdriven Guitar", "Distortion Guitar", "Guitar Harmonics",
        "Acoustic Bass", "Fingered Bass", "Picked Bass", "Fretless Bass", "Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2",
        "Violin", "Viola", "Cello", "Contrabass", "Tremolo Strings", "Pizzicato Strings", "Orchestral Harp", "Timpani",
        "String Ensemble 1", "String Ensemble 2", "Synth Strings 1", "Synth Strings 2", "Choir Aahs", "Voice Oohs", "Synth Voice", "Orchestra Hit",
        "Trumpet", "Trombone", "Tuba", "Muted Trumpet", "French Horn", "Brass Section", "Synth Brass 1", "Synth Brass 2",
        "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax", "Oboe", "English Horn", "Bassoon", "Clarinet",
        "Piccolo", "Flute", "Recorder", "Pan Flute", "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
        "Square Lead", "Saw Lead", "Calliope Lead", "Chiff Lead", "Charang Lead", "Voice Lead", "Fifths Lead", "Bass + Lead",
        "New Age Pad", "Warm Pad", "Polysynth", "Choir Pad", "Bowed Pad", "Metallic Pad", "Halo Pad", "Sweep Pad",
        "Rain FX", "Soundtrack FX", "Crystal FX", "Atmosphere FX", "Brightness FX", "Goblins FX", "Echoes FX", "Sci-fi FX",
        "Sitar", "Banjo", "Shamisen", "Koto", "Kalimba", "Bagpipe", "Fiddle", "Shanai",
        "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock", "Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
        "Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet", "Telephone Ring", "Helicopter", "Applause", "Gunshot"
    };

    return names[clampValue (program, 0, 127)];
}

void instrumentRange (int p, int& lo, int& hi)
{
    struct R { int from, to, lo, hi; };
    static const R table[] = {
        {   0,   7, 21, 108 }, {   8,   8, 60, 108 }, {   9,   9, 79, 108 }, {  10,  10, 60, 96 }, {  11,  11, 53, 89 },
        {  12,  12, 45,  96 }, {  13,  13, 65, 108 }, {  14,  14, 60,  77 }, {  15,  15, 48, 84 }, {  16,  20, 24, 96 },
        {  21,  21, 41,  89 }, {  22,  22, 60,  96 }, {  23,  23, 41,  89 }, {  24,  31, 40, 86 }, {  32,  32, 28, 55 },
        {  33,  37, 28,  60 }, {  38,  39, 24,  60 }, {  40,  40, 55, 100 }, {  41,  41, 48, 88 }, {  42,  42, 36, 76 },
        {  43,  43, 28,  55 }, {  44,  45, 28,  96 }, {  46,  46, 24, 103 }, {  47,  47, 38, 57 }, {  48,  51, 28, 100 },
        {  52,  53, 40,  81 }, {  54,  54, 40,  84 }, {  55,  55, 36,  84 }, {  56,  56, 52, 82 }, {  57,  57, 40, 72 },
        {  58,  58, 26,  58 }, {  59,  59, 52,  82 }, {  60,  60, 34,  77 }, {  61,  61, 34, 82 }, {  62,  63, 24, 96 },
        {  64,  64, 56,  88 }, {  65,  65, 49,  81 }, {  66,  66, 44,  76 }, {  67,  67, 36, 69 }, {  68,  68, 58, 91 },
        {  69,  69, 52,  81 }, {  70,  70, 34,  72 }, {  71,  71, 50,  91 }, {  72,  72, 74, 108 }, {  73,  79, 60, 96 },
        {  80,  87, 36, 108 }, {  88, 103, 24, 108 }, { 104, 104, 48,  84 }, { 105, 105, 48, 84 }, { 106, 106, 50, 86 },
        { 107, 107, 48,  91 }, { 108, 108, 60,  96 }, { 109, 109, 55,  81 }, { 110, 110, 55, 100 }, { 111, 111, 58, 89 },
        { 112, 112, 72, 108 }, { 113, 113, 60,  80 }, { 114, 114, 52,  88 }, { 116, 116, 36, 60 },
    };

    lo = 0;
    hi = 127;

    for (auto& r : table)
    {
        if (p >= r.from && p <= r.to)
        {
            lo = r.lo;
            hi = r.hi;
            return;
        }
    }
}

void normalizeSpec (StyleSpec& s)
{
    if (s.title.empty())
        s.title = "Untitled";

    s.tempo = clampValue (s.tempo, 40, 220);

    const bool validMeter = (s.tsDen == 4 && (s.tsNum == 2 || s.tsNum == 3 || s.tsNum == 4))
                         || (s.tsDen == 8 && (s.tsNum == 6 || s.tsNum == 12));

    if (! validMeter)
    {
        s.tsNum = 4;
        s.tsDen = 4;
    }

    s.bars = clampValue (s.bars, 1, 128);
    s.keyRoot = wrapPc (s.keyRoot);

    s.progression.erase (std::remove_if (s.progression.begin(), s.progression.end(),
                                         [] (const std::string& c) { return c.find_first_not_of (" \t") == std::string::npos; }),
                         s.progression.end());

    if (s.progression.size() > 32)
        s.progression.resize (32);

    s.progressionB.erase (std::remove_if (s.progressionB.begin(), s.progressionB.end(),
                                          [] (const std::string& c) { return c.find_first_not_of (" \t") == std::string::npos; }),
                          s.progressionB.end());

    if (s.progressionB.size() > 32)
        s.progressionB.resize (32);

    s.finalLift = clampValue (s.finalLift, 0, 5);

    static const char* lengths[] = { "auto", "sketch", "cue", "full", "extended", "bars" };

    if (std::find_if (std::begin (lengths), std::end (lengths), [&] (const char* l) { return s.length == l; }) == std::end (lengths))
        s.length = "auto";

    if (s.progression.empty())
    {
        const bool minorish = modeInfo (s.mode).relativeMajorOffset != 0;
        s.progression = minorish ? std::vector<std::string> { "i", "VI", "III", "VII" }
                                 : std::vector<std::string> { "I", "V", "vi", "IV" };
    }

    // Snap harmonic rhythm to something that divides the bar sensibly.
    {
        static const double allowed[] = { 0.25, 0.5, 1.0, 2.0, 4.0 };
        double best = 1.0, bestDist = 1e9;

        for (auto a : allowed)
        {
            const double d = std::abs (std::log2 (std::max (0.01, s.chordsPerBar)) - std::log2 (a));

            if (d < bestDist)
            {
                bestDist = d;
                best = a;
            }
        }

        s.chordsPerBar = best;

        const int beatsInBar = (s.tsDen == 8) ? s.tsNum / 3 : s.tsNum;

        while (s.chordsPerBar > 1.0 && std::fmod ((double) beatsInBar, s.chordsPerBar) != 0.0)
            s.chordsPerBar /= 2.0;
    }

    s.swing = clampValue (s.swing, 0.0, 1.0);
    s.energy = clampValue (s.energy, 0.0, 1.0);
    s.complexity = clampValue (s.complexity, 0.0, 1.0);
    s.humanize = clampValue (s.humanize, 0.0, 1.0);

    if (s.theme.size() > 64)
        s.theme.resize (64);

    for (auto& n : s.theme)
    {
        n.degree = clampValue (n.degree, -14, 21);
        n.beats = clampValue (n.beats, 0.125, 8.0);
    }

    if (s.tracks.size() > 24)
        s.tracks.resize (24);

    if (s.tracks.empty())
    {
        s.tracks.push_back ({ "Melody", Role::Melody,   0, -1, 60, 86, 0.5, 90, 0, "auto" });
        s.tracks.push_back ({ "Chords", Role::Chords,   0, -1, 48, 72, 0.5, 75, 0, "auto" });
        s.tracks.push_back ({ "Bass",   Role::Bass,    33, -1, 28, 52, 0.5, 90, 0, "auto" });
    }

    int nextChannel = 0;

    for (auto& t : s.tracks)
    {
        if (t.name.empty())
            t.name = roleDisplayName (t.role);

        t.program = clampValue (t.program, 0, 127);
        t.velocity = clampValue (t.velocity, 1, 127);
        t.density = clampValue (t.density, 0.0, 1.0);
        t.lowNote = clampValue (t.lowNote, 0, 127);
        t.highNote = clampValue (t.highNote, 0, 127);

        if (t.lowNote > t.highNote)
            std::swap (t.lowNote, t.highNote);

        if (t.role != Role::Percussion)
        {
            // Keep parts inside what the instrument can actually play...
            int instLo = 0, instHi = 127;
            instrumentRange (t.program, instLo, instHi);
            t.lowNote = clampValue (t.lowNote, instLo, instHi);
            t.highNote = clampValue (t.highNote, instLo, instHi);

            // ...while giving every pitched role about an octave and a half to move around in.
            const int wanted = std::min (18, instHi - instLo);

            while (t.highNote - t.lowNote < wanted)
            {
                if (t.highNote < instHi) ++t.highNote;
                if (t.highNote - t.lowNote < wanted && t.lowNote > instLo) --t.lowNote;
            }
        }

        t.entryBar = clampValue (t.entryBar, 0, std::max (0, s.bars - 1));

        if (t.mergeInto >= (int) s.tracks.size() || t.mergeInto == (int) (&t - s.tracks.data()))
            t.mergeInto = -1;

        const auto& valid = patternsForRole (t.role);
        t.pattern = lower (t.pattern);

        if (std::find (valid.begin(), valid.end(), t.pattern) == valid.end())
            t.pattern = "auto";

        if (t.role == Role::Percussion)
        {
            t.channel = 9;
        }
        else if (t.channel < 0 || t.channel > 15 || t.channel == 9)
        {
            if (nextChannel == 9)
                ++nextChannel;

            t.channel = nextChannel % 16;

            if (t.channel == 9)
                t.channel = 10;

            ++nextChannel;
        }
    }
}

std::string describeSpec (const StyleSpec& s)
{
    return keyName (s.keyRoot, s.mode) + " \xC2\xB7 " + std::to_string (s.tempo) + " BPM \xC2\xB7 "
         + std::to_string (s.tsNum) + "/" + std::to_string (s.tsDen) + " \xC2\xB7 " + std::to_string (s.bars) + " bars";
}

} // namespace pm
