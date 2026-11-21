#pragma once

#include "Composition.h"

#include <cstdint>
#include <vector>

namespace pm
{

/** Writes a Standard MIDI File (format 1): a conductor track with tempo, time and key
    signature, followed by one track per instrument with its name and program change.

    @param onlyTrack  -1 for every track, or the index of a single track to export
*/
std::vector<uint8_t> writeMidiFile (const Composition& composition, int onlyTrack = -1);

} // namespace pm
