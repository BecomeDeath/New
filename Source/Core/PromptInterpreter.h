#pragma once

#include "StyleSpec.h"

#include <cstdint>
#include <string>
#include <vector>

namespace pm
{

/** Values set with the plug-in's controls; they win over anything in the prompt. */
struct PromptOverrides
{
    int keyRoot = -1;  // 0-11, -1 = from prompt / style
    int mode = -1;     // a Mode value, -1 = from prompt / style
    int tempo = 0;     // BPM, 0 = from prompt / style
    int bars = 0;      // 0 = from prompt / style
};

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

/** The built-in genre templates, in display order. */
std::vector<StyleInfo> builtInStyles();

} // namespace pm
