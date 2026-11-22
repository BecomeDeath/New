#pragma once

#include "Composition.h"

#include <array>
#include <cstdint>
#include <vector>

namespace pm
{

/** How note articulations reach a sample library. */
enum class ArticulationOutput : uint8_t
{
    None,        // General MIDI: pizzicato / tremolo parts on their own tracks, nothing else
    Keyswitches, // a keyswitch note before each change of articulation
    Controller   // a controller value before each change (CC32 = Spitfire UACC)
};

/** Articulation -> keyswitch note or controller value, indexed by Articulation. -1 = not sent. */
struct ArticulationMap
{
    ArticulationOutput output = ArticulationOutput::None;
    int controller = 32;
    std::array<int, 9> values {};
    int leadTicks = 12;       // sent this long before the note it applies to

    /** Keyswitches from C-1 (MIDI 12) upwards: sustain, legato, staccato, spiccato, marcato,
        pizzicato, tremolo, trill. Below every orchestral instrument's range. */
    static ArticulationMap keyswitchDefaults();

    /** Spitfire UACC on CC32: long 1, legato 20, staccato 40, spiccato 42, marcato 52,
        pizzicato 56, tremolo 11, trill (major 2nd) 71. */
    static ArticulationMap uaccDefaults();

    int valueFor (Articulation a) const { return values[(size_t) a]; }
};

struct MidiExportOptions
{
    ArticulationMap articulations;
    bool markers = true;   // section markers and loop points in the conductor track
};

/** True for parts whose sample patches switch articulations (strings, woodwinds, brass). */
bool usesArticulations (const Track&);

/** Writes a Standard MIDI File (format 1): a conductor track with tempo, time and key
    signature, section markers and loop points, followed by one track per instrument with
    its name and program change.

    With keyswitch or controller output, pizzicato / tremolo parts are folded back into
    their section's track, and every change of articulation is announced just before the
    notes it applies to.

    @param onlyTrack  -1 for every track, or the index of a single track to export
*/
std::vector<uint8_t> writeMidiFile (const Composition& composition, int onlyTrack = -1, const MidiExportOptions& options = {});

} // namespace pm
