#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "MidiFileWriter.h"

/** Per-user settings shared by every PromptMIDI instance (API key, model, effort).

    Deliberately NOT stored in the plug-in state, so the API key never ends up inside a
    DAW project file that might be shared with others.
*/
class Settings
{
public:
    Settings();

    juce::String getApiKey() const;           // falls back to the ANTHROPIC_API_KEY environment variable
    bool hasApiKey() const                    { return getApiKey().isNotEmpty(); }
    bool apiKeyComesFromEnvironment() const;
    void setApiKey (const juce::String&);

    juce::String getModel() const;
    void setModel (const juce::String&);

    juce::String getEffort() const;
    void setEffort (const juce::String&);

    /** How exported MIDI carries articulations: "none" (General MIDI), "keyswitch" or "cc". */
    juce::String getArticulationOutput() const;
    void setArticulationOutput (const juce::String&);

    /** Eight numbers in the order sustain, legato, staccato, spiccato, marcato, pizzicato,
        tremolo, trill (keyswitch notes or controller values; -1 = not sent). Empty = defaults. */
    juce::String getArticulationValues() const;
    void setArticulationValues (const juce::String&);

    int getArticulationController() const;
    void setArticulationController (int);

    /** The export map built from the three settings above. */
    pm::ArticulationMap getArticulationMap() const;

    /** The values text for a mode's defaults ("keyswitch" or "cc"). */
    static juce::String defaultArticulationValues (const juce::String& output);

    /** What the preview plays through: "" = the built-in instruments (General MIDI SoundFont),
        "sketch" = the small sketch synth, or the full path of your own .sf2 SoundFont. */
    juce::String getPreviewSound() const;
    void setPreviewSound (const juce::String&);

    /** Updates: whether to look for a newer version when the window opens (default on), when it
        last looked, the newest version it found, and a version the user chose to skip. */
    /** How long PromptMIDI thinks before it answers (pm::ThinkingDepth): 0 Quick, 1 Thorough, 2 Deep. */
    int getThinkingDepth() const;
    void setThinkingDepth (int);

    bool getCheckForUpdates() const;
    void setCheckForUpdates (bool);
    juce::int64 getLastUpdateCheck() const;
    void setLastUpdateCheck (juce::int64 millisecondsSinceEpoch);
    juce::String getKnownRelease() const;            // the newest latest.json seen, as JSON
    void setKnownRelease (const juce::String&);
    juce::String getSkippedVersion() const;
    void setSkippedVersion (const juce::String&);

    juce::File getFile() const;

    static constexpr const char* defaultModel = "claude-opus-5-5";

private:
    juce::PropertiesFile* props();
    const juce::PropertiesFile* props() const;

    mutable juce::ApplicationProperties appProperties;
};

/** One Settings object for all plug-in instances in the process. */
using SharedSettings = juce::SharedResourcePointer<Settings>;
