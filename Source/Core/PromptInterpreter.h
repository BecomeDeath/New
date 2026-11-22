#pragma once

#include "StyleSpec.h"

#include <cstdint>
#include <string>
#include <vector>

namespace pm
{

/** An instrument the user picked to compose for. */
struct InstrumentChoice
{
    std::string name;            // as listed in instrumentCatalogue(), e.g. "Celli"
    int program = 0;             // General MIDI program; -1 = percussion
    std::string role = "auto";   // "auto" or a role id (melody, countermelody, chords, pad, arpeggio, bass, ostinato, percussion)
};

/** Everything the instrument picker offers, by category. */
struct CatalogueEntry
{
    std::string category, name;
    int program = 0;             // -1 = percussion
    int low = 48, high = 84;
    Role role = Role::Melody;    // what it usually does
    std::string pattern;
    bool polyphonic = false;     // can play melody and accompaniment at once (piano, harp, guitar...)
};

const std::vector<CatalogueEntry>& instrumentCatalogue();

/** Values set with the plug-in's controls; they win over anything in the prompt. */
struct PromptOverrides
{
    int keyRoot = -1;  // 0-11, -1 = from prompt / style
    int mode = -1;     // a Mode value, -1 = from prompt / style
    int tempo = 0;     // BPM, 0 = from prompt / style
    int bars = 0;      // 0 = from prompt / style
    std::string length; // "" = from prompt / style; else sketch | cue | full | extended
    std::vector<InstrumentChoice> instruments; // empty = from the prompt / style
};

/** Rebuilds the spec's tracks from the user's instruments: roles are kept where the user set
    them and assigned like an arranger would otherwise (the most melodic part takes the tune, the
    lowest the bass, keyboards and guitars the chords, harps the arpeggios, sections the pads).
    One polyphonic instrument becomes a solo piece (melody and accompaniment on one track); one
    melodic instrument plays the tune alone. Small groups are composed as songs, three or more
    pitched instruments in an orchestral style as a full cue. Existing tracks with the same name
    (e.g. from Claude's plan) lend their roles and patterns. */
void applyInstrumentChoices (StyleSpec&, const std::vector<InstrumentChoice>&);

/** Offline prompt interpreter.

    Reads a free-text request such as "Generate me a fantasy orchestral midi" or
    "soft piano in D minor, 16 bars, 70 bpm, no drums" and turns it into a StyleSpec:
    a genre template, mood adjustments, instruments to add/remove, and explicit
    parameters (key, mode, tempo, bars, length in seconds, time signature).

    The seed picks between equally plausible choices (tempo within the style's range,
    which progression, which mode), so the same prompt with another seed is a variation.
*/
StyleSpec interpretPrompt (const std::string& prompt, uint64_t seed, const PromptOverrides& overrides = {});

struct StyleInfo
{
    std::string id;
    std::string displayName;
};

/** A new take on a plan, for the Variation button: the same key, mode, tempo, length and
    instruments, with new harmony (unless keepHarmony, e.g. for a plan Claude wrote) and new
    accompaniment figures. Compose the result with a new seed for a new theme, form and
    orchestration. */
StyleSpec varySpec (const StyleSpec& spec, uint64_t seed, bool keepHarmony = false);

/** The built-in genre templates, in display order. */
std::vector<StyleInfo> builtInStyles();

} // namespace pm
