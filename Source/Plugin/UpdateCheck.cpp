#include "UpdateCheck.h"

namespace updates
{
const char* const manifestUrl  = "https://github.com/BecomeDeath/PromptMIDI/releases/latest/download/latest.json";
const char* const downloadPage = "https://becomedeath.github.io/TLComposer/promptmidi.html#download";

juce::String currentVersion()
{
   #ifdef PROMPTMIDI_VERSION
    return PROMPTMIDI_VERSION;
   #else
    return "0.0.0";
   #endif
}

static juce::Array<int> versionParts (juce::String v)
{
    v = v.trim().trimCharactersAtStart ("vV").upToFirstOccurrenceOf ("-", false, false);

    juce::StringArray tokens;
    tokens.addTokens (v, ".", {});

    juce::Array<int> parts;

    for (const auto& t : tokens)
        parts.add (t.getIntValue());

    while (parts.size() < 3)
        parts.add (0);

    return parts;
}

int compareVersions (const juce::String& a, const juce::String& b)
{
    const auto x = versionParts (a), y = versionParts (b);

    for (int i = 0; i < juce::jmax (x.size(), y.size()); ++i)
    {
        const int p = i < x.size() ? x[i] : 0, q = i < y.size() ? y[i] : 0;

        if (p != q)
            return p < q ? -1 : 1;
    }

    return 0;
}

juce::String platformKey()
{
   #if JUCE_WINDOWS
    return "windows";
   #elif JUCE_MAC
    return "macos";
   #else
    return "linux";
   #endif
}

Release parseManifest (const juce::String& json, const juce::String& platform)
{
    Release r;
    const auto v = juce::JSON::parse (json);

    if (! v.isObject())
        return r;

    const auto version = v["version"].toString().trim().trimCharactersAtStart ("vV");

    if (version.isEmpty() || ! juce::CharacterFunctions::isDigit (version[0]))
        return r;

    // Only web links are ever opened.
    const auto web = [] (const juce::var& u) { const auto s = u.toString().trim(); return s.startsWith ("https://") ? s : juce::String(); };

    r.version  = version;
    r.notes    = v["notes"].toString().trim().substring (0, 400);
    r.page     = web (v["page"]);
    r.download = web (v["downloads"][juce::Identifier (platform)]);
    return r;
}

//==============================================================================
Checker::Checker()
    : juce::Thread ("PromptMIDI update check"),
      alive (std::make_shared<std::atomic<bool>> (true))
{
}

Checker::~Checker()
{
    alive->store (false);
    signalThreadShouldExit();

    {
        const juce::ScopedLock sl (lock);

        if (stream != nullptr)
            stream->cancel();
    }

    stopThread (4000);
}

void Checker::start (std::function<void (const Release&)> onDone, const juce::String& url)
{
    if (isThreadRunning())
        return;

    address = url;
    callback = std::move (onDone);
    startThread (juce::Thread::Priority::low);
}

void Checker::run()
{
    juce::WebInputStream in (juce::URL (address), false);
    in.withConnectionTimeout (8000)
      .withNumRedirectsToFollow (5)
      .withExtraHeaders ("Accept: application/json\r\nUser-Agent: PromptMIDI/" + currentVersion());

    {
        const juce::ScopedLock sl (lock);
        stream = &in;
    }

    juce::String body;

    if (! threadShouldExit() && in.connect (nullptr) && in.getStatusCode() == 200)
        body = in.readEntireStreamAsString();

    {
        const juce::ScopedLock sl (lock);
        stream = nullptr;
    }

    if (threadShouldExit())
        return;

    juce::MessageManager::callAsync ([flag = alive, done = callback, release = parseManifest (body)]
    {
        if (flag->load() && done)
            done (release);
    });
}
}
