#pragma once

#include "Composition.h"

namespace pm
{

/** Checks a finished composition the way an orchestrator would proof a score:

    - every note inside its instrument's practical range
    - harmony parts (sustained, marcato, spiccato, pizzicato, tremolo notes) only play notes
      of the chord they sit on (or the pedal underneath it)
    - no harsh minor 2nds / minor 9ths on the beat that involve a non-chord tone
    - no close-interval mud below C3
    - the lead line moves mostly by step, has a real range and ends on the tonic

    The score (0-10) is used to pick the best of several takes; the notes explain it.
*/
QualityReport checkQuality (const Composition&);

/** Fixes what can be fixed mechanically without changing the music's intent:
    folds out-of-range notes by octaves and moves harmony notes that clash on the beat
    with the melody to the nearest chord tone that doesn't. Returns the number of edits.
*/
int repairComposition (Composition&);

} // namespace pm
