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

juce::String Settings::getPreviewSound() const
{
    return props() != nullptr ? props()->getValue ("previewSound").trim() : juce::String();
}

void Settings::setPreviewSound (const juce::String& sound)
{
    if (auto* p = props())
    {
        p->setValue ("previewSound", sound.trim());
        p->saveIfNeeded();
    }
}

bool Settings::getCheckForUpdates() const
{
    return props() == nullptr || props()->getBoolValue ("checkForUpdates", true);
}

void Settings::setCheckForUpdates (bool on)
{
    if (auto* p = props())
    {
        p->setValue ("checkForUpdates", on);
        p->saveIfNeeded();
    }
}

juce::int64 Settings::getLastUpdateCheck() const
{
    return props() != nullptr ? props()->getValue ("lastUpdateCheck").getLargeIntValue() : 0;
}

void Settings::setLastUpdateCheck (juce::int64 when)
{
    if (auto* p = props())
    {
        p->setValue ("lastUpdateCheck", juce::String (when));
        p->saveIfNeeded();
    }
}

juce::String Settings::getKnownRelease() const
{
    return props() != nullptr ? props()->getValue ("knownRelease") : juce::String();
}

void Settings::setKnownRelease (const juce::String& json)
{
    if (auto* p = props())
    {
        p->setValue ("knownRelease", json);
        p->saveIfNeeded();
    }
}

juce::String Settings::getSkippedVersion() const
{
    return props() != nullptr ? props()->getValue ("skippedVersion").trim() : juce::String();
}

void Settings::setSkippedVersion (const juce::String& version)
{
    if (auto* p = props())
    {
        p->setValue ("skippedVersion", version.trim());
        p->saveIfNeeded();
    }
}

juce::String Settings::getArticulationOutput() const
{
    const auto o = props() != nullptr ? props()->getValue ("articulationOutput").trim().toLowerCase() : juce::String();
    return (o == "keyswitch" || o == "cc") ? o : juce::String ("none");
}

void Settings::setArticulationOutput (const juce::String& output)
{
    if (auto* p = props())
    {
        p->setValue ("articulationOutput", output);
        p->saveIfNeeded();
    }
}

juce::String Settings::getArticulationValues() const
{
    return props() != nullptr ? props()->getValue ("articulationValues").trim() : juce::String();
}

void Settings::setArticulationValues (const juce::String& values)
{
    if (auto* p = props())
    {
        p->setValue ("articulationValues", values.trim());
        p->saveIfNeeded();
    }
}

int Settings::getArticulationController() const
{
    return props() != nullptr ? juce::jlimit (0, 127, props()->getIntValue ("articulationController", 32)) : 32;
}

void Settings::setArticulationController (int cc)
{
    if (auto* p = props())
    {
        p->setValue ("articulationController", juce::jlimit (0, 127, cc));
        p->saveIfNeeded();
    }
}

namespace
{
    // The settings list articulations in this order; the map is indexed by pm::Articulation.
    const pm::Articulation settingsOrder[] = { pm::Articulation::Sustain, pm::Articulation::Legato, pm::Articulation::Staccato,
                                               pm::Articulation::Spiccato, pm::Articulation::Marcato, pm::Articulation::Pizzicato,
                                               pm::Articulation::Tremolo, pm::Articulation::Trill };
}

juce::String Settings::defaultArticulationValues (const juce::String& output)
{
    const auto map = output == "cc" ? pm::ArticulationMap::uaccDefaults() : pm::ArticulationMap::keyswitchDefaults();
    juce::StringArray values;

    for (auto a : settingsOrder)
        values.add (juce::String (map.valueFor (a)));

    return values.joinIntoString (" ");
}

pm::ArticulationMap Settings::getArticulationMap() const
{
    const auto output = getArticulationOutput();

    if (output == "none")
        return {};

    auto map = output == "cc" ? pm::ArticulationMap::uaccDefaults() : pm::ArticulationMap::keyswitchDefaults();
    map.controller = getArticulationController();

    juce::StringArray tokens;
    tokens.addTokens (getArticulationValues(), " ,;", "");
    tokens.removeEmptyStrings();

    for (int i = 0; i < tokens.size() && i < 8; ++i)
        map.values[(size_t) settingsOrder[i]] = juce::jlimit (-1, 127, tokens[i].getIntValue());

    map.values[(size_t) pm::Articulation::Default] = map.values[(size_t) pm::Articulation::Sustain];
    return map;
}

juce::File Settings::getFile() const
{
    return props() != nullptr ? props()->getFile() : juce::File();
}
