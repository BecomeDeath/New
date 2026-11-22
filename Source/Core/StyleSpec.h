#pragma once

#include "MusicTheory.h"

#include <string>
#include <vector>

namespace pm
{

/** What a track does in the arrangement. */
enum class Role
{
    Melody,
    CounterMelody,
    Chords,
    Pad,
    Arpeggio,
    Bass,
    Ostinato,
    Percussion
};

enum class Dynamics
{
    Flat,
    Build,
    Arc,
    Fade
};

const char* roleId (Role);
const char* roleDisplayName (Role);
bool roleFromString (const std::string&, Role&);
const std::vector<Role>& allRoles();

const char* dynamicsId (Dynamics);
bool dynamicsFromString (const std::string&, Dynamics&);

/** Playing patterns that make sense for a role ("auto" is always first). */
const std::vector<std::string>& patternsForRole (Role);

/** Every pattern id across all roles (used for the JSON schema enum). */
const std::vector<std::string>& allPatternIds();

/** General MIDI instrument name for a 0-based program number. */
const char* gmProgramName (int program);

/** Practical sounding range of a General MIDI instrument (MIDI notes). */
void instrumentRange (int program, int& lowest, int& highest);

/** One note of a hand-written theme: a scale step relative to the tonic, and a duration in quarter notes. */
struct ThemeNote
{
    int degree = 0;      // 0 = tonic, 1 = next scale note up, -1 = scale note below the tonic...
    double beats = 1.0;  // duration in quarter notes
    bool rest = false;
};

struct TrackSpec
{
    std::string name;
    Role role = Role::Melody;
    int program = 0;          // General MIDI program 0-127 (ignored for percussion)
    int channel = -1;         // 0-15, -1 = assign automatically (percussion always uses 9)
    int lowNote = 48;         // register the track is confined to
    int highNote = 84;
    double density = 0.5;     // 0 = very sparse, 1 = very busy
    int velocity = 90;        // base loudness 1-127
    int entryBar = 0;         // bar (0-based) where the track comes in
    std::string pattern = "auto";
    int endBar = -1;          // bar where the part stops (exclusive); -1 = plays to the end
};

/** A complete, engine-independent description of the piece to compose.
    Produced either by the offline prompt interpreter or by Claude.
*/
struct StyleSpec
{
    std::string title = "Untitled";
    std::string styleId;
    int tempo = 100;
    int tsNum = 4;
    int tsDen = 4;
    int bars = 16;
    int keyRoot = 0;
    Mode mode = Mode::Major;
    std::vector<std::string> progression { "I", "V", "vi", "IV" };
    std::vector<std::string> progressionB;  // contrasting harmony for B sections (empty = same as A)
    int finalLift = 0;                       // semitones the last A section moves up (0 = no key change)
    double chordsPerBar = 1.0;
    double swing = 0.0;             // 0 = straight, 1 = full triplet swing
    bool swingSixteenths = false;   // swing 16ths (hip hop) rather than 8ths (jazz)
    double energy = 0.5;
    double complexity = 0.3;        // chord colour: 7ths, 9ths
    double humanize = 0.5;
    Dynamics dynamics = Dynamics::Arc;
    bool resolveEnding = true;      // false = seamless loop (cues get loop markers and an end stinger)
    bool orchestral = false;        // compose as a full orchestral cue (form, development, orchestration)
    std::string length = "auto";    // sketch | cue | full | extended | bars (use `bars` as given) | auto
    std::vector<ThemeNote> theme;   // optional main theme for the melody
    std::vector<TrackSpec> tracks;

    std::vector<std::string> interpretation; // human-readable notes on how the prompt was read
};

/** Clamps every field into a range the composer can handle and assigns MIDI channels. */
void normalizeSpec (StyleSpec&);

/** One-line description, e.g. "D Minor · 92 BPM · 4/4 · 16 bars". */
std::string describeSpec (const StyleSpec&);

} // namespace pm
