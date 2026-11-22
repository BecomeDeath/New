#pragma once

#include "Composition.h"

#include <cstdint>

namespace pm
{

/** The orchestral cue engine.

    Composes a complete game/film cue from a StyleSpec whose `orchestral` flag is set:

    - Form: intro, theme, restatement, development, contrast, build, climax, outro (or an end
      stinger with loop markers for loopable cues), sized from the requested length (16 bars
      up to about four minutes).
    - Harmony: phrase-level progressions with half and authentic cadences, secondary dominants,
      borrowed chords, tonic and dominant pedal points, descending-fifths sequences, a contrast
      section in the relative key, a key lift for the climax, and melody-aware
      reharmonisation of every restatement of the theme.
    - Themes: an 8-bar period theme, developed by fragmentation and sequence, augmentation,
      imitation between sections (call and response), and a second theme for the contrast.
    - Orchestration: the spec's tracks are treated as an orchestra; each section reassigns who
      carries the theme, the counter-line, the harmony (voiced across the section, with divisi),
      the rhythmic engine (spiccato ostinati, pizzicato, harp) and the bass. Notes carry
      articulations; pizzicato and tremolo get their own tracks.
    - Transitions: swells into tutti hits, timpani / snare / cymbal rolls, string and flute runs,
      harp glissandi; orchestral percussion as separate instruments.
*/
Composition composeCue (const StyleSpec& spec, uint64_t seed);

} // namespace pm
