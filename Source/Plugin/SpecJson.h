#pragma once

#include <juce_core/juce_core.h>

#include "Composition.h"
#include "StyleSpec.h"

/** JSON conversions for the engine types.

    The same shape is used for three things: the structured output Claude returns,
    the spec saved in the plug-in state, and the schema that constrains Claude.
*/
namespace SpecJson
{
    juce::var specToVar (const pm::StyleSpec&);

    /** Lenient: missing fields keep their defaults, out-of-range values are clamped. */
    bool specFromVar (const juce::var&, pm::StyleSpec& out, juce::String& error);

    juce::var compositionToVar (const pm::Composition&);
    bool compositionFromVar (const juce::var&, pm::Composition& out);

    /** JSON Schema for Claude's structured output (every object closed, every field required). */
    juce::var outputSchema();

    juce::String keyName (int pitchClass);
}
