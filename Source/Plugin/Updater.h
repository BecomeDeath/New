#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "UpdateCheck.h"

#include <atomic>

/** Updates PromptMIDI from inside the plug-in, the way a game launcher does: it downloads the new
    version's installer, checks it against the SHA-256 published with the release, and installs it
    over the current one with the parts and folders chosen the first time. No browser, no wizard.

    The plug-in lives in a system folder, so the system asks once for permission (Windows: "Yes";
    macOS: your password). A DAW keeps the plug-in it has loaded until it closes:
    - macOS, Linux: the files are replaced at once; the new version loads the next time the DAW does.
    - Windows: files in use can't be replaced, so the update is installed as soon as the DAW closes.
    - The standalone app installs, quits and restarts into the new version.

    One updater serves every PromptMIDI window in the process (juce::SharedResourcePointer). */
class Updater : private juce::Thread
{
public:
    enum class Stage { Idle, Downloading, Verifying, Installing, WaitingForHost, Done, Failed };

    struct State
    {
        Stage stage = Stage::Idle;
        juce::String version;
        juce::int64 downloaded = 0, total = 0;
        juce::String message;        // what is happening, or what to do next
        bool restartApp = false;     // the standalone app should quit now; it restarts into the new version
    };

    Updater();
    ~Updater() override;

    /** Downloads and installs `release` (one update at a time). */
    void start (const updates::Release&);
    /** Stops a download (nothing is installed). */
    void cancel();
    /** Back to Idle after Done or Failed. */
    void reset();

    State getState() const;
    bool isBusy() const;

    /** How it runs here. The defaults describe this process; tests and tools change them. */
    struct Context
    {
        bool standalone = false;
        juce::String hostName;           // for the messages, e.g. "Ableton Live"
        int hostProcessId = 0;           // the process that has the plug-in loaded
        juce::File app;                  // the standalone app, to restart
        juce::File runningBinary;        // the plug-in or app binary, to find where it is installed
        juce::File downloadFolder;

        enum class Elevation { Ask, Sudo, None };
        Elevation elevation = Elevation::Ask;   // Ask: the system's prompt; Sudo/None: tests on CI runners
        bool restartAfterInstall = true;        // the standalone app restarts (off in tests)
        bool dryRun = false;                    // stop after downloading and verifying (tests)
    };

    Context context;

    //==========================================================================
    // What runs, built separately so it can be tested.

    /** Windows: the PowerShell that waits for the host (or app) to close, runs the installer silently
        with the earlier choices, and restarts the app if asked. */
    static juce::String windowsScript (const juce::String& setupPath, int waitForProcessId, const juce::String& logPath, const juce::String& restartPath);

    /** macOS: installer(8) choice changes that leave out these choices ("au", "app", ...). */
    static juce::String macChoices (const juce::StringArray& leaveOut);

    /** macOS: the parts installed now, at the standard places ("vst3", "au", "app", "readme"). Empty
        when PromptMIDI was installed somewhere else (Customize > Location). */
    static juce::StringArray macInstalledParts (const juce::File& root = juce::File ("/"));

    /** Linux: install.sh arguments that reproduce an earlier install, from its list of installed
        files (installed-files, next to uninstall.sh). */
    struct LinuxInstall
    {
        bool ok = false;
        bool systemWide = false;     // under /usr: needs root
        juce::StringArray args;
    };

    static LinuxInstall linuxInstallFor (const juce::StringArray& installedFiles, const juce::File& dataFolder);

    /** Linux: the installed-files list of the install this binary belongs to, if any. */
    static juce::File linuxInstalledFiles (const juce::File& runningBinary);

private:
    void run() override;
    bool download (juce::File& result);
    bool verify (const juce::File&);
    void install (const juce::File&);
    void installWindows (const juce::File&);
    void installMac (const juce::File&);
    void installLinux (const juce::File&);
    bool runChild (const juce::StringArray& command, juce::String& output, int timeoutMs);
    void set (Stage, const juce::String& message = {});
    void fail (const juce::String& message);
    juce::String restartLater();
    juce::String hostText() const;

    mutable juce::CriticalSection lock;
    State state;
    updates::Release release;
    std::atomic<bool> cancelled { false };

    JUCE_DECLARE_NON_COPYABLE (Updater)
};
