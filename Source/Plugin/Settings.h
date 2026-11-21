#pragma once

#include <juce_data_structures/juce_data_structures.h>

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

    juce::File getFile() const;

    static constexpr const char* defaultModel = "claude-opus-5-5";

private:
    juce::PropertiesFile* props();
    const juce::PropertiesFile* props() const;

    mutable juce::ApplicationProperties appProperties;
};

/** One Settings object for all plug-in instances in the process. */
using SharedSettings = juce::SharedResourcePointer<Settings>;
