#pragma once

#include "StyleSpec.h"

#include <string>
#include <vector>

namespace pm
{

constexpr int kPpq = 480; // ticks per quarter note

struct Note
{
    int start = 0;    // ticks
    int length = 0;   // ticks
    int pitch = 60;
    int velocity = 100;
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
};

struct ChordSlot
{
    int start = 0;
    int end = 0;
    std::string name;   // absolute name, e.g. "Dm7"
    std::string roman;  // as written in the progression, e.g. "i7"
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
    Mode mode = Mode::Major;
    std::vector<Track> tracks;
    std::vector<ChordSlot> chords;
    std::vector<std::string> progression; // one cycle, as written (roman numerals)

    std::string summary() const;
    int noteCount() const;
};

} // namespace pm
