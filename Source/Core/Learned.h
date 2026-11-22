#pragma once

#include "ComposerDetail.h"
#include "LearnedData.h"

#include <array>
#include <string>
#include <vector>

/** Using the statistics learned from the MIDI corpus (LearnedData): style-family weights for a
    spec, bar rhythms for melodies, interval probabilities for the melody scorer, and chord
    progressions for harmony.
*/
namespace pm::detail
{

/** A melody bar rhythm drawn from the corpus for this meter, with about the density asked for.
    Empty when there is no data for the meter (the caller falls back to its own rhythms).
    @param needDownbeat  the bar must start with a note or a rest, never a tie
*/
std::vector<RNote> learnedBarRhythm (const Meter&, double density, MelStyle, const FamilyMix&, bool needDownbeat, Rng&);

/** log P(next step | the two steps before), in diatonic scale steps, smoothed and mixed over the
    families. prev2 / prev1 may be kNoIndex at a phrase start. */
double learnedStepLogProb (const FamilyMix&, bool minor, int prev2, int prev1, int next);

/** A four-chord progression from the corpus, weighted by the family mix, valid in `mode`
    (major, minor or harmonic minor), different from every progression in `avoid`.
    Empty if the mode has no learned data. */
std::vector<std::string> learnedProgression (const FamilyMix&, Mode, Rng&, const std::vector<std::vector<std::string>>& avoid = {});

} // namespace pm::detail
