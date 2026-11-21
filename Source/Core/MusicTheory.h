#pragma once

#include <string>
#include <vector>

namespace pm
{

enum class Mode
{
    Major,
    Minor,
    Dorian,
    Phrygian,
    Lydian,
    Mixolydian,
    Locrian,
    HarmonicMinor,
    MelodicMinor,
    PentatonicMajor,
    PentatonicMinor,
    Blues,
    DoubleHarmonic,
    Japanese
};

struct ModeInfo
{
    Mode mode;
    const char* id;            // stable identifier used in JSON / state
    const char* displayName;
    std::vector<int> melodyScale;  // the notes melodies are drawn from
    std::vector<int> harmonyScale; // 7-note parent scale used for roman numerals
    int relativeMajorOffset;       // semitones from the relative major's tonic up to this tonic
};

const std::vector<ModeInfo>& allModes();
const ModeInfo& modeInfo (Mode);
bool modeFromString (const std::string& text, Mode& out);

int wrapPc (int pitchClass);
int floorDiv (int a, int b);

std::string pitchClassName (int pitchClass, bool preferFlats);
bool pitchClassFromString (const std::string& text, int& pitchClassOut);
bool keyPrefersFlats (int keyRoot, Mode);
std::string keyName (int keyRoot, Mode);

/** Number of sharps (positive) or flats (negative) for a MIDI key-signature event. */
int keySignatureAccidentals (int keyRoot, Mode);

struct Chord
{
    int rootPc = 0;
    std::vector<int> intervals { 0, 4, 7 }; // semitones above the root, ascending, first is 0
    std::string symbol;                     // the text it was parsed from (e.g. "V7/V")

    std::vector<int> pitchClasses() const;
    bool hasInterval (int semitones) const;
    bool containsPc (int pitchClass) const;
    bool hasSeventh() const;
};

/** Parses a roman numeral ("i", "bVII", "V7/V", "iim7b5", "IVmaj7", "vii°") relative to a key,
    or, as a fallback, an absolute chord name ("Am", "F#m7", "Bbmaj9", "Gsus4").
*/
bool parseChordSymbol (const std::string& text, int keyRoot, Mode mode, Chord& out);

/** The diatonic triad (or seventh chord) built on a 0-based scale degree of the mode. */
Chord diatonicChord (int degree, int keyRoot, Mode mode, bool withSeventh);

/** Adds the scale's own seventh above the root, if the root belongs to the scale. */
void addDiatonicSeventh (Chord& chord, int keyRoot, Mode mode);

/** Adds a ninth if it belongs to the scale. */
void addDiatonicNinth (Chord& chord, int keyRoot, Mode mode);

std::string chordName (const Chord& chord, bool preferFlats);

/** Scale-step <-> MIDI pitch mapping for a given scale and tonic pitch. */
struct ScaleMap
{
    std::vector<int> intervals;
    int tonicPitch = 60;

    int size() const { return (int) intervals.size(); }
    int pitch (int index) const;
    int indexNear (int midiPitch) const;
    int indexAtOrAbove (int midiPitch) const;
    int indexAtOrBelow (int midiPitch) const;
};

} // namespace pm
