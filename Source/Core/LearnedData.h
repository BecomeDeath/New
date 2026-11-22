#pragma once

#include <cstdint>

/** Statistics learned from a MIDI corpus (see Tools/corpus/learn_from_midicaps.py and the
    attribution in LearnedData.cpp). Only aggregate counts are stored, never a melody or a piece.
*/
namespace pm::learned
{

/** Style families the statistics are grouped by (from the corpus's genre and mood labels). */
enum Family : uint8_t { All = 0, Cinematic, Dark, Emotional, Bright, Energetic, Jazz, NumFamilies };

/** How often the lead line moved by `next` diatonic steps after moving `prev2`, then `prev1`. */
struct MelodyCount
{
    uint8_t family, minor;
    int8_t prev2, prev1, next;
    uint32_t count;
};

/** A bar of the lead line on a 16th-note grid: where notes start, and where a note sounds. */
struct RhythmPattern
{
    uint8_t family, meter;    // meter: 0 = 4/4, 1 = 3/4, 2 = 6/8, 3 = 2/4
    uint16_t onsets, sounding;
    uint32_t count;
};

/** A four-chord progression in roman numerals, relative to the key. */
struct Progression
{
    uint8_t family, minor;
    const char* romans;
    uint32_t count;
};

extern const MelodyCount melodyCounts[];
extern const int numMelodyCounts;
extern const RhythmPattern rhythmPatterns[];
extern const int numRhythmPatterns;
extern const Progression progressions[];
extern const int numProgressions;

} // namespace pm::learned
