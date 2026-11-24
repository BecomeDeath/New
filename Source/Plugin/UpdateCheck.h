#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <memory>

/** "An update is available".

    Every release publishes a small file, latest.json, next to its installers (see
    .github/workflows/build.yml). When PromptMIDI's window opens it downloads that file from
    the newest release, at most twice a day, and compares the version with its own. Nothing is
    sent: it's a plain download of a public file. It can be turned off in Settings. */
namespace updates
{
    /** This build's version (CMake's project version). */
    juce::String currentVersion();

    /** Where the newest release's latest.json lives. */
    extern const char* const manifestUrl;

    /** Where people download PromptMIDI. */
    extern const char* const downloadPage;

    /** -1, 0 or 1 for dotted version numbers: "1.10.0" is newer than "1.9.2"; a leading "v" and
        anything after a "-" are ignored. */
    int compareVersions (const juce::String& a, const juce::String& b);

    /** "windows", "macos" or "linux". */
    juce::String platformKey();

    struct Release
    {
        juce::String version;    // "1.2.0"
        juce::String notes;      // a line or two about what's new
        juce::String download;   // this platform's installer (https only)
        juce::String page;       // the download page (https only)
        juce::String sha256;     // the installer's SHA-256 (64 hex digits), for installing it from inside PromptMIDI
        juce::int64 size = 0;    // its size in bytes (0 = not published)
        juce::String json;       // the manifest as read

        bool isValid() const     { return version.isNotEmpty(); }
        bool isNewer() const     { return isValid() && compareVersions (version, currentVersion()) > 0; }

        /** Releases from 1.2 on publish a checksum, so PromptMIDI can download and install them itself. */
        bool canInstallInApp() const  { return download.isNotEmpty() && sha256.length() == 64; }
    };

    /** Reads latest.json: { "version", "notes", "page", "downloads": { "windows", "macos", "linux" },
        "sha256": { ... }, "sizes": { ... } }. Returns an invalid release for anything else. */
    Release parseManifest (const juce::String& json, const juce::String& platform = platformKey());

    /** Downloads the manifest on a background thread and calls back on the message thread (with
        an invalid release if it couldn't be read). Destroying the checker cancels the download
        and drops the callback, so it can't outlive the window that owns it. */
    class Checker : private juce::Thread
    {
    public:
        Checker();
        ~Checker() override;

        void start (std::function<void (const Release&)> onDone, const juce::String& url = manifestUrl);
        bool isChecking() const  { return isThreadRunning(); }

    private:
        void run() override;

        juce::String address;
        std::function<void (const Release&)> callback;
        std::shared_ptr<std::atomic<bool>> alive;
        juce::CriticalSection lock;
        juce::WebInputStream* stream = nullptr;

        JUCE_DECLARE_NON_COPYABLE (Checker)
    };
}
