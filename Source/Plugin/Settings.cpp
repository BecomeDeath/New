#include "Settings.h"

Settings::Settings()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "PromptMIDI";
    o.filenameSuffix = ".settings";
   #if JUCE_LINUX || JUCE_BSD
    o.folderName = ".config/PromptMIDI";
   #else
    o.folderName = "PromptMIDI";
   #endif
    o.osxLibrarySubFolder = "Application Support";
    o.commonToAllUsers = false;
    o.ignoreCaseOfKeyNames = true;
    o.millisecondsBeforeSaving = 0;
    appProperties.setStorageParameters (o);
}

juce::PropertiesFile* Settings::props()             { return appProperties.getUserSettings(); }
const juce::PropertiesFile* Settings::props() const { return appProperties.getUserSettings(); }

juce::String Settings::getApiKey() const
{
    auto key = props() != nullptr ? props()->getValue ("apiKey").trim() : juce::String();

    if (key.isEmpty())
        key = juce::SystemStats::getEnvironmentVariable ("ANTHROPIC_API_KEY", {}).trim();

    return key;
}

bool Settings::apiKeyComesFromEnvironment() const
{
    return (props() == nullptr || props()->getValue ("apiKey").trim().isEmpty())
        && juce::SystemStats::getEnvironmentVariable ("ANTHROPIC_API_KEY", {}).trim().isNotEmpty();
}

void Settings::setApiKey (const juce::String& key)
{
    if (auto* p = props())
    {
        p->setValue ("apiKey", key.trim());
        p->saveIfNeeded();
    }
}

juce::String Settings::getModel() const
{
    const auto m = props() != nullptr ? props()->getValue ("model").trim() : juce::String();
    return m.isNotEmpty() ? m : juce::String (defaultModel);
}

void Settings::setModel (const juce::String& model)
{
    if (auto* p = props())
    {
        p->setValue ("model", model.trim());
        p->saveIfNeeded();
    }
}

juce::String Settings::getEffort() const
{
    const auto e = props() != nullptr ? props()->getValue ("effort").trim() : juce::String();
    return (e == "low" || e == "medium" || e == "high") ? e : juce::String ("medium");
}

void Settings::setEffort (const juce::String& effort)
{
    if (auto* p = props())
    {
        p->setValue ("effort", effort);
        p->saveIfNeeded();
    }
}

juce::File Settings::getFile() const
{
    return props() != nullptr ? props()->getFile() : juce::File();
}
