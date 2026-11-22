#pragma once

#include "StyleSpec.h"

#include <cstdint>
#include <string>
#include <vector>

namespace pm
{

constexpr int kPpq = 480; // ticks per quarter note

/** How a note is played. Written as note length / velocity shaping and, optionally, keyswitches. */
enum class Articulation : uint8_t
{
    Default = 0,
    Legato,     // connected melodic line
    Sustain,    // long notes (pads, held chords)
    Staccato,
    Spiccato,   // short, bouncing strings (ostinatos)
    Marcato,    // accented, punchy
    Pizzicato,
    Tremolo,
    Trill
};

const char* articulationName (Articulation);

struct Note
{
    int start = 0;    // ticks
    int length = 0;   // ticks
    int pitch = 60;
    int velocity = 100;
    Articulation articulation = Articulation::Default;
};

/** A named point in the piece (section starts, loop points), written as MIDI markers. */
struct Marker
{
    int tick = 0;
    std::string text;
};

struct ControlChange
{
    int tick = 0;
    int controller = 11;
    int value = 100;
};

struct Track
{
    std::string name;
    Role role = Role::Melody;
    int program = 0;
    int channel = 0;
    bool isDrum = false;
    std::vector<Note> notes;
    std::vector<ControlChange> controls;
    int splitFrom = -1;   // for a "(pizz.)" / "(trem.)" part: the index of the section it was split from
};

struct ChordSlot
{
    int start = 0;
    int end = 0;
    std::string name;   // absolute name, e.g. "Dm7"
    std::string roman;  // as written in the progression, e.g. "i7"
    std::vector<int> pcs;  // pitch classes of the chord
    int bassPc = -1;       // pedal point under the chord, if any
};

/** The automatic check every generation goes through (see QualityCheck.h). */
struct QualityReport
{
    double score = 0.0;            // 0-10
    int notes = 0;
    int outOfRange = 0;            // notes outside the instrument's practical range
    int wrongChordNotes = 0;       // harmony notes that aren't in the chord
    double strongClashPct = 0.0;   // beats with a harsh minor 2nd / minor 9th involving a non-chord tone
    double lowMudPct = 0.0;        // beats with close intervals below C3
    double melodyStepPct = 0.0;
    int melodyRange = 0;
    bool endsOnTonic = true;
    int candidates = 1;            // how many takes were composed to pick this one
    std::vector<std::string> notesText;
};

struct Composition
{
    std::string title;
    int ppq = kPpq;
    int tempo = 120;
    int tsNum = 4;
    int tsDen = 4;
    int bars = 0;
    int ticksPerBar = 4 * kPpq;
    int lengthTicks = 0;
    int keyRoot = 0;
    int endKeyRoot = 0;   // differs from keyRoot when the last section lifts to a new key
    Mode mode = Mode::Major;
    std::vector<Track> tracks;
    std::vector<ChordSlot> chords;
    std::vector<std::string> progression; // one cycle, as written (roman numerals)
    std::vector<Marker> markers;
    int loopStart = -1;                   // ticks; -1 = no loop region (the whole piece loops)
    int loopEnd = -1;
    std::string form;                     // e.g. "Intro - Theme - Development - Climax - Outro"
    QualityReport quality;

    double seconds() const { return tempo > 0 ? (double) lengthTicks / kPpq * 60.0 / tempo : 0.0; }

    std::string summary() const;
    int noteCount() const;
};

} // namespace pm
