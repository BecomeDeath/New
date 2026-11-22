#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Composition.h"

#include <vector>

/** Presets and MIDI files in and out of PromptMIDI.

    A preset (.promptmidi, JSON) holds a prompt and the controls - and, when saved from a piece,
    the piece itself with its edits - so it can be loaded again, shared and imported elsewhere.
    MIDI files from anywhere can be imported to view, edit, preview and drag out.
*/
namespace PresetIO
{
    constexpr const char* presetExtension = ".promptmidi";

    /** Documents/PromptMIDI/Presets - where Save puts presets and the menu lists them from. */
    juce::File presetsFolder();

    /** The presets saved there, by name. */
    juce::Array<juce::File> userPresets();

    /** Presets that come with PromptMIDI: a prompt and a length, composed fresh when loaded. */
    struct Factory
    {
        const char* name;
        const char* prompt;
        const char* length;     // "" = auto, or sketch | cue | full | extended
    };

    const std::vector<Factory>& factoryPresets();

    /** Reads a Standard MIDI File into a composition: tracks (split by channel), programs,
        notes, sustain and expression controllers, tempo, meter, key (from the key signature or
        the notes) and markers. Returns false with a reason if it can't. */
    bool readMidiFile (const juce::File&, pm::Composition& out, juce::String& error);

    /** The key a set of notes is in (Krumhansl-Schmuckler profiles). */
    void detectKey (const pm::Composition&, int& keyRoot, bool& minor);
}
