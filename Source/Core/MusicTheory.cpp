#include "MusicTheory.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace pm
{

namespace
{
    std::string toLower (std::string s)
    {
        for (auto& c : s)
            c = (char) std::tolower ((unsigned char) c);

        return s;
    }

    std::string trim (const std::string& s)
    {
        size_t a = 0, b = s.size();

        while (a < b && std::isspace ((unsigned char) s[a]))
            ++a;

        while (b > a && std::isspace ((unsigned char) s[b - 1]))
            --b;

        return s.substr (a, b - a);
    }

    bool contains (const std::string& haystack, const char* needle)
    {
        return haystack.find (needle) != std::string::npos;
    }

    /** Replaces the handful of unicode music symbols people (and LLMs) like to use. */
    std::string asciiChordText (std::string s)
    {
        struct Rep { const char* from; const char* to; };
        static const Rep reps[] = {
            { "\xE2\x99\xAD", "b" },   // flat sign
            { "\xE2\x99\xAF", "#" },   // sharp sign
            { "\xC2\xB0", "o" },       // degree sign (diminished)
            { "\xE2\x97\x8B", "o" },   // white circle
            { "\xC3\xB8", "h" },       // o-slash (half diminished)
            { "\xC3\x98", "h" },
            { "\xCE\x94", "M7" },      // delta (major seventh)
            { "\xE2\x88\x86", "M7" },  // increment sign
            { "\xE2\x80\x93", "-" },
        };

        for (auto& r : reps)
        {
            const std::string from (r.from);

            for (size_t pos = s.find (from); pos != std::string::npos; pos = s.find (from, pos))
            {
                s.replace (pos, from.size(), r.to);
                pos += std::char_traits<char>::length (r.to);
            }
        }

        return s;
    }

    const int majorScale[7] = { 0, 2, 4, 5, 7, 9, 11 };

    /** Builds intervals for a chord quality given the case of the numeral and suffix text. */
    std::vector<int> intervalsFromSuffix (const std::string& suffixRaw, bool majorThirdByDefault)
    {
        const std::string suffix = suffixRaw;
        const std::string lower = toLower (suffix);

        bool halfDim = contains (lower, "m7b5") || contains (lower, "h");
        bool dim = ! halfDim && (contains (lower, "dim") || contains (lower, "o"));
        bool aug = contains (lower, "+") || contains (lower, "aug");
        bool sus2 = contains (lower, "sus2");
        bool sus4 = ! sus2 && contains (lower, "sus");
        bool add9 = contains (lower, "add9") || contains (lower, "add2");
        bool six = contains (lower, "6") && ! contains (lower, "add6");
        int seventh = 0; // 0 none, 1 flat seventh, 2 major seventh
        bool ninth = false;

        if (contains (suffix, "M7") || contains (lower, "maj7") || contains (suffix, "M9") || contains (lower, "maj9"))
        {
            seventh = 2;
            ninth = contains (suffix, "M9") || contains (lower, "maj9");
        }
        else if (! add9 && contains (lower, "9"))
        {
            seventh = 1;
            ninth = true;
        }
        else if (contains (lower, "7") || contains (lower, "11") || contains (lower, "13"))
        {
            seventh = 1;
        }

        // An explicit "m" (as in "Im7" or "Vm") forces a minor third even on an uppercase numeral.
        bool forcedMinor = false;

        if (! lower.empty() && lower[0] == 'm' && ! contains (lower, "maj"))
            forcedMinor = true;

        if (halfDim)
            return { 0, 3, 6, 10 };

        int third = (majorThirdByDefault && ! forcedMinor) ? 4 : 3;
        int fifth = 7;

        if (dim) { third = 3; fifth = 6; }
        if (aug) { third = 4; fifth = 8; }

        std::vector<int> iv { 0 };

        if (sus2)       iv.push_back (2);
        else if (sus4)  iv.push_back (5);
        else            iv.push_back (third);

        iv.push_back (fifth);

        if (six)
            iv.push_back (9);

        if (seventh == 1)
            iv.push_back (dim ? 9 : 10);
        else if (seventh == 2)
            iv.push_back (11);

        if (ninth || add9)
            iv.push_back (14);

        std::sort (iv.begin(), iv.end());
        iv.erase (std::unique (iv.begin(), iv.end()), iv.end());
        return iv;
    }

    bool parseRoman (const std::string& textIn, int keyRoot, Mode mode, Chord& out)
    {
        std::string text = trim (textIn);

        if (text.empty())
            return false;

        // Secondary chords: "V7/V", "vii°/ii"
        const auto slash = text.find ('/');

        if (slash != std::string::npos)
        {
            Chord target;

            if (parseRoman (text.substr (slash + 1), keyRoot, mode, target))
            {
                Chord lhs;

                if (! parseRoman (text.substr (0, slash), target.rootPc, Mode::Major, lhs))
                    return false;

                out = lhs;
                out.symbol = textIn;
                return true;
            }

            text = text.substr (0, slash); // treat anything else after '/' (e.g. inversions) as noise
        }

        size_t i = 0;
        int accidental = 0;

        while (i < text.size() && (text[i] == 'b' || text[i] == '#'))
        {
            // "b" followed by a numeral character is an accidental; we only accept it when what
            // follows is a roman numeral letter.
            if (i + 1 >= text.size())
                return false;

            const char next = (char) std::tolower ((unsigned char) text[i + 1]);

            if (next != 'i' && next != 'v' && next != 'b' && next != '#')
                return false;

            accidental += text[i] == 'b' ? -1 : 1;
            ++i;
        }

        static const char* numerals[] = { "vii", "vi", "v", "iv", "iii", "ii", "i" };
        static const int degrees[]    = { 6,     5,    4,   3,    2,     1,    0   };

        const std::string rest = toLower (text.substr (i));
        int degree = -1;
        size_t numeralLength = 0;

        for (int n = 0; n < 7; ++n)
        {
            const std::string num (numerals[n]);

            if (rest.compare (0, num.size(), num) == 0)
            {
                degree = degrees[n];
                numeralLength = num.size();
                break;
            }
        }

        if (degree < 0)
            return false;

        const bool upper = std::isupper ((unsigned char) text[i]) != 0;
        const std::string suffix = text.substr (i + numeralLength);

        // Reject things that are obviously not chord suffixes
        for (char c : suffix)
            if (! (std::isalnum ((unsigned char) c) || c == '+' || c == '#' || c == '(' || c == ')' || c == '-' || c == ' '))
                return false;

        const auto& hs = modeInfo (mode).harmonyScale;

        out.rootPc = wrapPc (keyRoot + hs[(size_t) degree] + accidental);
        out.intervals = intervalsFromSuffix (suffix, upper);
        out.symbol = textIn;
        return true;
    }

    bool parseAbsoluteChord (const std::string& textIn, Chord& out)
    {
        const std::string text = trim (textIn);

        if (text.empty())
            return false;

        const char letter = (char) std::toupper ((unsigned char) text[0]);

        if (letter < 'A' || letter > 'G')
            return false;

        size_t i = 1;
        int pc = 0;

        if (! pitchClassFromString (text.substr (0, 1), pc))
            return false;

        while (i < text.size() && (text[i] == '#' || text[i] == 'b'))
        {
            pc += text[i] == '#' ? 1 : -1;
            ++i;
        }

        std::string suffix = text.substr (i);
        const auto slash = suffix.find ('/');

        if (slash != std::string::npos)
            suffix = suffix.substr (0, slash);

        bool minor = false;
        std::string lower = toLower (suffix);

        if ((lower.rfind ("min", 0) == 0) || (! suffix.empty() && suffix[0] == 'm' && lower.rfind ("maj", 0) != 0))
            minor = true;

        out.rootPc = wrapPc (pc);
        out.intervals = intervalsFromSuffix (suffix, ! minor);
        out.symbol = textIn;
        return true;
    }
}

//==============================================================================
int wrapPc (int x)            { return ((x % 12) + 12) % 12; }
int floorDiv (int a, int b)   { return (a >= 0) ? a / b : -((-a + b - 1) / b); }

const std::vector<ModeInfo>& allModes()
{
    static const std::vector<ModeInfo> modes = {
        { Mode::Major,           "major",            "Major",            { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 4, 5, 7, 9, 11 }, 0 },
        { Mode::Minor,           "minor",            "Minor",            { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 8, 10 }, 9 },
        { Mode::Dorian,          "dorian",           "Dorian",           { 0, 2, 3, 5, 7, 9, 10 }, { 0, 2, 3, 5, 7, 9, 10 }, 2 },
        { Mode::Phrygian,        "phrygian",         "Phrygian",         { 0, 1, 3, 5, 7, 8, 10 }, { 0, 1, 3, 5, 7, 8, 10 }, 4 },
        { Mode::Lydian,          "lydian",           "Lydian",           { 0, 2, 4, 6, 7, 9, 11 }, { 0, 2, 4, 6, 7, 9, 11 }, 5 },
        { Mode::Mixolydian,      "mixolydian",       "Mixolydian",       { 0, 2, 4, 5, 7, 9, 10 }, { 0, 2, 4, 5, 7, 9, 10 }, 7 },
        { Mode::Locrian,         "locrian",          "Locrian",          { 0, 1, 3, 5, 6, 8, 10 }, { 0, 1, 3, 5, 6, 8, 10 }, 11 },
        { Mode::HarmonicMinor,   "harmonic_minor",   "Harmonic Minor",   { 0, 2, 3, 5, 7, 8, 11 }, { 0, 2, 3, 5, 7, 8, 11 }, 9 },
        { Mode::MelodicMinor,    "melodic_minor",    "Melodic Minor",    { 0, 2, 3, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 9, 11 }, 9 },
        { Mode::PentatonicMajor, "pentatonic_major", "Major Pentatonic", { 0, 2, 4, 7, 9 },        { 0, 2, 4, 5, 7, 9, 11 }, 0 },
        { Mode::PentatonicMinor, "pentatonic_minor", "Minor Pentatonic", { 0, 3, 5, 7, 10 },       { 0, 2, 3, 5, 7, 8, 10 }, 9 },
        { Mode::Blues,           "blues",            "Blues",            { 0, 3, 5, 6, 7, 10 },    { 0, 2, 4, 5, 7, 9, 10 }, 0 },
        { Mode::DoubleHarmonic,  "double_harmonic",  "Double Harmonic",  { 0, 1, 4, 5, 7, 8, 11 }, { 0, 1, 4, 5, 7, 8, 11 }, 4 },
        { Mode::Japanese,        "japanese",         "Japanese (In)",    { 0, 1, 5, 7, 8 },        { 0, 1, 3, 5, 7, 8, 10 }, 4 },
    };

    return modes;
}

const ModeInfo& modeInfo (Mode m)
{
    for (auto& mi : allModes())
        if (mi.mode == m)
            return mi;

    return allModes().front();
}

bool modeFromString (const std::string& textIn, Mode& out)
{
    std::string t = toLower (trim (textIn));

    for (auto& c : t)
        if (c == ' ' || c == '-')
            c = '_';

    for (auto& mi : allModes())
    {
        if (t == mi.id || t == toLower (mi.displayName))
        {
            out = mi.mode;
            return true;
        }
    }

    struct Synonym { const char* name; Mode mode; };
    static const Synonym synonyms[] = {
        { "ionian", Mode::Major },            { "maj", Mode::Major },
        { "aeolian", Mode::Minor },           { "natural_minor", Mode::Minor },     { "min", Mode::Minor },
        { "harmonic", Mode::HarmonicMinor },  { "melodic", Mode::MelodicMinor },
        { "pentatonic", Mode::PentatonicMajor }, { "major_pentatonic", Mode::PentatonicMajor },
        { "minor_pentatonic", Mode::PentatonicMinor },
        { "hijaz", Mode::DoubleHarmonic },    { "arabic", Mode::DoubleHarmonic },   { "byzantine", Mode::DoubleHarmonic },
        { "in", Mode::Japanese },             { "in_scale", Mode::Japanese },       { "hirajoshi", Mode::Japanese },
    };

    for (auto& s : synonyms)
    {
        if (t == s.name)
        {
            out = s.mode;
            return true;
        }
    }

    return false;
}

std::string pitchClassName (int pc, bool preferFlats)
{
    static const char* sharps[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* flats[]  = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    return (preferFlats ? flats : sharps)[wrapPc (pc)];
}

bool pitchClassFromString (const std::string& textIn, int& out)
{
    const std::string text = trim (textIn);

    if (text.empty())
        return false;

    static const int base[] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
    const char letter = (char) std::toupper ((unsigned char) text[0]);

    if (letter < 'A' || letter > 'G')
        return false;

    int pc = base[letter - 'A'];
    const std::string rest = toLower (text.substr (1));

    if (rest.empty())                                        {}
    else if (rest == "#" || rest == "sharp" || rest == "s")  pc += 1;
    else if (rest == "b" || rest == "flat")                  pc -= 1;
    else if (rest == "##")                                   pc += 2;
    else if (rest == "bb")                                   pc -= 2;
    else                                                     return false;

    out = wrapPc (pc);
    return true;
}

bool keyPrefersFlats (int keyRoot, Mode mode)
{
    const int relMajor = wrapPc (keyRoot - modeInfo (mode).relativeMajorOffset);
    return relMajor == 5 || relMajor == 10 || relMajor == 3 || relMajor == 8 || relMajor == 1 || relMajor == 6;
}

std::string keyName (int keyRoot, Mode mode)
{
    return pitchClassName (keyRoot, keyPrefersFlats (keyRoot, mode)) + " " + modeInfo (mode).displayName;
}

int keySignatureAccidentals (int keyRoot, Mode mode)
{
    static const int sharpsForMajor[] = { 0, -5, 2, -3, 4, -1, -6, 1, -4, 3, -2, 5 };
    return sharpsForMajor[wrapPc (keyRoot - modeInfo (mode).relativeMajorOffset)];
}

//==============================================================================
std::vector<int> Chord::pitchClasses() const
{
    std::vector<int> pcs;

    for (auto iv : intervals)
    {
        const int pc = wrapPc (rootPc + iv);

        if (std::find (pcs.begin(), pcs.end(), pc) == pcs.end())
            pcs.push_back (pc);
    }

    return pcs;
}

bool Chord::hasInterval (int semitones) const
{
    return std::find (intervals.begin(), intervals.end(), semitones) != intervals.end();
}

bool Chord::containsPc (int pc) const
{
    for (auto iv : intervals)
        if (wrapPc (rootPc + iv) == wrapPc (pc))
            return true;

    return false;
}

bool Chord::hasSeventh() const
{
    return hasInterval (10) || hasInterval (11) || (hasInterval (9) && hasInterval (6));
}

bool parseChordSymbol (const std::string& textIn, int keyRoot, Mode mode, Chord& out)
{
    const std::string text = asciiChordText (textIn);

    if (parseRoman (text, keyRoot, mode, out))
    {
        out.symbol = trim (textIn);
        return true;
    }

    if (parseAbsoluteChord (text, out))
    {
        out.symbol = trim (textIn);
        return true;
    }

    return false;
}

Chord diatonicChord (int degree, int keyRoot, Mode mode, bool withSeventh)
{
    const auto& hs = modeInfo (mode).harmonyScale;
    const int d = ((degree % 7) + 7) % 7;
    const int root = hs[(size_t) d];

    auto stepAbove = [&] (int steps)
    {
        const int idx = d + steps;
        const int note = hs[(size_t) (idx % 7)] + 12 * (idx / 7);
        return note - root;
    };

    Chord c;
    c.rootPc = wrapPc (keyRoot + root);
    c.intervals = { 0, stepAbove (2), stepAbove (4) };

    if (withSeventh)
        c.intervals.push_back (stepAbove (6));

    static const char* upperNumerals[] = { "I", "II", "III", "IV", "V", "VI", "VII" };
    static const char* lowerNumerals[] = { "i", "ii", "iii", "iv", "v", "vi", "vii" };
    const bool major = c.intervals[1] == 4;
    c.symbol = major ? upperNumerals[d] : lowerNumerals[d];

    if (c.intervals[2] == 6)
        c.symbol += "o";

    if (withSeventh)
        c.symbol += "7";

    return c;
}

void addDiatonicSeventh (Chord& chord, int keyRoot, Mode mode)
{
    if (chord.hasSeventh() || chord.intervals.size() < 3)
        return;

    const auto& hs = modeInfo (mode).harmonyScale;

    for (int d = 0; d < 7; ++d)
    {
        if (wrapPc (keyRoot + hs[(size_t) d]) != chord.rootPc)
            continue;

        const int seventhPc = wrapPc (keyRoot + hs[(size_t) ((d + 6) % 7)]);
        const int iv = wrapPc (seventhPc - chord.rootPc);

        if (iv == 10 || iv == 11)
        {
            // A major seventh on top of a minor triad (mMaj7) is rarely wanted; skip it.
            if (iv == 11 && chord.hasInterval (3))
                return;

            chord.intervals.push_back (iv);
            std::sort (chord.intervals.begin(), chord.intervals.end());
        }

        return;
    }

    // Chromatic chord: a major triad gets a dominant seventh, everything else stays a triad.
    if (chord.hasInterval (4) && chord.hasInterval (7))
    {
        chord.intervals.push_back (10);
        std::sort (chord.intervals.begin(), chord.intervals.end());
    }
}

void addDiatonicNinth (Chord& chord, int keyRoot, Mode mode)
{
    if (chord.hasInterval (14) || chord.hasInterval (2))
        return;

    const int ninthPc = wrapPc (chord.rootPc + 2);
    const auto& hs = modeInfo (mode).harmonyScale;

    for (auto s : hs)
    {
        if (wrapPc (keyRoot + s) == ninthPc)
        {
            chord.intervals.push_back (14);
            std::sort (chord.intervals.begin(), chord.intervals.end());
            return;
        }
    }
}

std::string chordName (const Chord& c, bool preferFlats)
{
    auto has = [&] (int iv) { return c.hasInterval (iv); };

    const bool m3 = has (3), M3 = has (4);
    const bool s2 = has (2) && ! m3 && ! M3;
    const bool s4 = has (5) && ! m3 && ! M3;
    const bool d5 = has (6), p5 = has (7), a5 = has (8);
    const bool d7 = has (9) && d5 && m3 && ! p5 && ! has (10);
    const bool m7 = has (10), M7 = has (11);
    const bool six = has (9) && ! d7;
    const bool nine = has (14);

    std::string q;
    bool seventhWritten = false;

    if (m3 && d5 && ! p5)
    {
        if (d7)       { q = "dim7"; seventhWritten = true; }
        else if (m7)  { q = "m7b5"; seventhWritten = true; }
        else          q = "dim";
    }
    else if (M3 && a5 && ! p5)
    {
        q = "aug";
    }
    else if (m3)
    {
        q = "m";
    }

    if (! seventhWritten)
    {
        if (M7)       q += (q == "m") ? "Maj7" : "maj7";
        else if (m7)  q += "7";
    }

    if (nine)
    {
        if (! q.empty() && q.back() == '7' && q != "dim7" && q != "m7b5")
            q.back() = '9';
        else
            q += "add9";
    }

    if (six)  q += "6";
    if (s2)   q += "sus2";
    if (s4)   q += "sus4";

    return pitchClassName (c.rootPc, preferFlats) + q;
}

//==============================================================================
int ScaleMap::pitch (int index) const
{
    const int n = size();
    const int oct = floorDiv (index, n);
    const int step = index - oct * n;
    return tonicPitch + 12 * oct + intervals[(size_t) step];
}

int ScaleMap::indexNear (int midiPitch) const
{
    const int n = size();
    const int approx = floorDiv ((midiPitch - tonicPitch) * n, 12);
    int best = approx, bestDist = 1000;

    for (int i = approx - n - 1; i <= approx + n + 1; ++i)
    {
        const int d = std::abs (pitch (i) - midiPitch);

        if (d < bestDist)
        {
            bestDist = d;
            best = i;
        }
    }

    return best;
}

int ScaleMap::indexAtOrAbove (int midiPitch) const
{
    int i = indexNear (midiPitch);

    while (pitch (i) < midiPitch)
        ++i;

    return i;
}

int ScaleMap::indexAtOrBelow (int midiPitch) const
{
    int i = indexNear (midiPitch);

    while (pitch (i) > midiPitch)
        --i;

    return i;
}

} // namespace pm
