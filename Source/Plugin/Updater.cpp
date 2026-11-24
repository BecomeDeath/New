#include "Updater.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_cryptography/juce_cryptography.h>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <shellapi.h>
#else
 #include <unistd.h>
#endif

namespace
{
int currentProcessId()
{
   #if JUCE_WINDOWS
    return (int) GetCurrentProcessId();
   #else
    return (int) getpid();
   #endif
}

/** A PowerShell single-quoted string. */
juce::String psQuote (const juce::String& s)
{
    return "'" + s.replace ("'", "''") + "'";
}

juce::String fileNameFor (const juce::String& url)
{
    const auto name = juce::File::createLegalFileName (url.fromLastOccurrenceOf ("/", false, false).upToFirstOccurrenceOf ("?", false, false));
    return name.isEmpty() ? juce::String ("PromptMIDI-update") : name;
}
} // namespace

//==============================================================================
Updater::Updater() : juce::Thread ("PromptMIDI updater")
{
    context.standalone = juce::JUCEApplicationBase::isStandaloneApp();
    context.hostName = context.standalone ? juce::String() : juce::String (juce::PluginHostType().getHostDescription());
    context.hostProcessId = currentProcessId();
    context.app = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    context.runningBinary = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    context.downloadFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PromptMIDI-update");
}

Updater::~Updater()
{
    cancelled = true;
    signalThreadShouldExit();
    stopThread (15000);
}

void Updater::start (const updates::Release& r)
{
    if (isThreadRunning())
        return;

    {
        const juce::ScopedLock sl (lock);
        release = r;
        state = {};
        state.stage = Stage::Downloading;
        state.version = r.version;
        state.total = r.size;
        state.message = "Downloading PromptMIDI " + r.version;
    }

    cancelled = false;
    startThread (juce::Thread::Priority::normal);
}

void Updater::cancel()
{
    cancelled = true;
}

void Updater::reset()
{
    if (isThreadRunning())
        return;

    const juce::ScopedLock sl (lock);
    state = {};
}

Updater::State Updater::getState() const
{
    const juce::ScopedLock sl (lock);
    return state;
}

bool Updater::isBusy() const
{
    const auto s = getState().stage;
    return s == Stage::Downloading || s == Stage::Verifying || s == Stage::Installing;
}

void Updater::set (Stage s, const juce::String& message)
{
    const juce::ScopedLock sl (lock);
    state.stage = s;
    state.message = message;
}

void Updater::fail (const juce::String& message)
{
    set (Stage::Failed, message);
}

juce::String Updater::hostText() const
{
    if (context.standalone)
        return "PromptMIDI";

    return context.hostName.isEmpty() || context.hostName == "Unknown" ? juce::String ("your DAW") : context.hostName;
}

//==============================================================================
void Updater::run()
{
    juce::File installer;

    if (! download (installer))
        return;

    set (Stage::Verifying, "Checking the download");

    if (! verify (installer))
        return;

    if (context.dryRun)
    {
        set (Stage::Done, "Verified " + installer.getFileName() + " (not installed: dry run).");
        return;
    }

    set (Stage::Installing,
        #if JUCE_WINDOWS
         context.elevation == Context::Elevation::Ask ? "Click Yes when Windows asks to let PromptMIDI install the update." : "Installing"
        #elif JUCE_MAC
         context.elevation == Context::Elevation::Ask ? "Enter your Mac password when asked: the plug-in lives in a system folder." : "Installing"
        #else
         "Installing"
        #endif
        );

    install (installer);
}

bool Updater::download (juce::File& result)
{
    const auto folder = context.downloadFolder.getChildFile (juce::File::createLegalFileName (release.version));
    folder.createDirectory();
    const auto target = folder.getChildFile (fileNameFor (release.download));
    const auto part = folder.getChildFile (target.getFileName() + ".part");

    // Downloaded before (an earlier try)? Use it if it checks out.
    if (target.existsAsFile() && juce::SHA256 (target).toHexString().toLowerCase() == release.sha256)
    {
        result = target;
        return true;
    }

    int status = 0;
    auto stream = juce::URL (release.download).createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs (20000)
            .withNumRedirectsToFollow (10)
            .withStatusCode (&status)
            .withExtraHeaders ("User-Agent: PromptMIDI/" + updates::currentVersion()));

    if (stream == nullptr || (status != 0 && status != 200))
    {
        fail ("Couldn't download the update (" + (status != 0 ? "HTTP " + juce::String (status) : juce::String ("no connection"))
              + "). Check your internet connection and try again.");
        return false;
    }

    juce::int64 total = stream->getTotalLength();

    if (total <= 0)
        total = release.size;

    {
        const juce::ScopedLock sl (lock);
        state.total = total;
        state.downloaded = 0;
    }

    part.deleteFile();
    juce::int64 done = 0;
    bool ok = true;

    {
        juce::FileOutputStream out (part);

        if (! out.openedOk())
        {
            fail ("Couldn't save the download in " + folder.getFullPathName() + ".");
            return false;
        }

        juce::HeapBlock<char> buffer (65536);

        while (! stream->isExhausted())
        {
            if (cancelled || threadShouldExit())
            {
                ok = false;
                break;
            }

            const int n = stream->read (buffer, 65536);

            if (n <= 0)
                break;

            if (! out.write (buffer, (size_t) n))
            {
                out.flush();
                part.deleteFile();
                fail ("Couldn't save the download (is the disk full?).");
                return false;
            }

            done += n;
            const juce::ScopedLock sl (lock);
            state.downloaded = done;
        }

        out.flush();
    }

    if (! ok)
    {
        part.deleteFile();
        set (Stage::Idle, "Download cancelled.");
        return false;
    }

    if (total > 0 && done != total)
    {
        part.deleteFile();
        fail ("The download was cut off. Try again.");
        return false;
    }

    target.deleteFile();

    if (! part.moveFileTo (target))
    {
        fail ("Couldn't save the download in " + folder.getFullPathName() + ".");
        return false;
    }

    result = target;
    return true;
}

bool Updater::verify (const juce::File& file)
{
    // Only what was published with the release is ever installed.
    if (release.size > 0 && file.getSize() != release.size)
    {
        file.deleteFile();
        fail ("The download is the wrong size, so it wasn't installed. Try again.");
        return false;
    }

    if (juce::SHA256 (file).toHexString().toLowerCase() != release.sha256)
    {
        file.deleteFile();
        fail ("The download doesn't match the checksum published with the release, so it wasn't installed. Try again later.");
        return false;
    }

    return true;
}

void Updater::install (const juce::File& file)
{
   #if JUCE_WINDOWS
    installWindows (file);
   #elif JUCE_MAC
    installMac (file);
   #else
    installLinux (file);
   #endif
}

bool Updater::runChild (const juce::StringArray& command, juce::String& output, int timeoutMs)
{
    juce::ChildProcess child;

    if (! child.start (command, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    {
        output = "couldn't start " + command[0];
        return false;
    }

    // Read as it goes, so a chatty process never blocks on a full pipe.
    const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    char buffer[4096];

    while (child.isRunning() && juce::Time::getMillisecondCounter() < until)
    {
        const int n = child.readProcessOutput (buffer, sizeof (buffer));

        if (n > 0)
            output += juce::String::fromUTF8 (buffer, n);
        else
            juce::Thread::sleep (50);
    }

    output += child.readAllProcessOutput();

    if (child.isRunning())
    {
        child.kill();
        output += "\n(timed out)";
        return false;
    }

    return child.getExitCode() == 0;
}

/** Starts the app again a moment after this process quits. */
juce::String Updater::restartLater()
{
   #if ! JUCE_WINDOWS
    juce::ChildProcess relaunch;
    const juce::StringArray command
       #if JUCE_MAC
        { "/bin/sh", "-c", "sleep 1; /usr/bin/open -n \"$0\"", context.app.getFullPathName() };
       #else
        { "/bin/sh", "-c", "sleep 1; exec \"$0\" > /dev/null 2>&1", context.app.getFullPathName() };
       #endif

    if (! relaunch.start (command, 0))
        return "Updated. Start PromptMIDI again to use the new version.";
   #endif

    return {};
}

//==============================================================================
juce::String Updater::windowsScript (const juce::String& setupPath, int waitForProcessId, const juce::String& logPath, const juce::String& restartPath)
{
    juce::String s;
    s << "$ErrorActionPreference = 'SilentlyContinue'\r\n";

    // Files in use can't be replaced: wait until the DAW (or the app) has closed.
    if (waitForProcessId > 0)
        s << "Wait-Process -Id " << waitForProcessId << "\r\n"
          << "Start-Sleep -Milliseconds 500\r\n";

    // The same parts and folders as last time (the installer remembers them), no windows.
    s << "$p = Start-Process -FilePath " << psQuote (setupPath)
      << " -ArgumentList " << psQuote ("/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /NOCLOSEAPPLICATIONS /LOG=\"" + logPath + "\"")
      << " -Wait -PassThru\r\n"
      << "Set-Content -LiteralPath " << psQuote (logPath + ".exit") << " -Value $p.ExitCode\r\n";

    // Back into the app, as the user rather than as administrator.
    if (restartPath.isNotEmpty())
        s << "Start-Process -FilePath 'explorer.exe' -ArgumentList " << psQuote ("\"" + restartPath + "\"") << "\r\n";

    return s;
}

void Updater::installWindows (const juce::File& file)
{
   #if JUCE_WINDOWS
    const auto log = file.getSiblingFile ("install.log");
    log.deleteFile();
    log.getSiblingFile ("install.log.exit").deleteFile();

    const bool restart = context.standalone && context.restartAfterInstall;
    const auto script = windowsScript (file.getFullPathName(), context.hostProcessId, log.getFullPathName(),
                                       restart ? context.app.getFullPathName() : juce::String());

    // -EncodedCommand takes UTF-16LE in base64: no quoting to get wrong.
    juce::MemoryOutputStream utf16;

    for (auto p = script.getCharPointer(); ! p.isEmpty();)
    {
        auto c = (juce::uint32) p.getAndAdvance();

        if (c >= 0x10000)
        {
            c -= 0x10000;
            utf16.writeShort ((short) (0xD800 + (c >> 10)));
            utf16.writeShort ((short) (0xDC00 + (c & 0x3FF)));
        }
        else
        {
            utf16.writeShort ((short) c);
        }
    }

    const auto parameters = "-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -EncodedCommand "
                          + juce::Base64::toBase64 (utf16.getData(), utf16.getDataSize());

    SHELLEXECUTEINFOW info {};
    info.cbSize = sizeof (info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = context.elevation == Context::Elevation::Ask ? L"runas" : L"open";
    info.lpFile = L"powershell.exe";
    info.lpParameters = parameters.toWideCharPointer();
    info.nShow = SW_HIDE;

    if (! ShellExecuteExW (&info))
    {
        const auto error = GetLastError();
        fail (error == ERROR_CANCELLED ? juce::String ("Windows didn't get permission to install the update, so nothing changed. Press Update now to try again.")
                                       : "Couldn't start the installer (Windows error " + juce::String ((int) error) + ").");
        return;
    }

    if (info.hProcess != nullptr)
        CloseHandle (info.hProcess);

    if (restart)
    {
        const juce::ScopedLock sl (lock);
        state.stage = Stage::Done;
        state.restartApp = true;
        state.message = "Restarting PromptMIDI to finish the update...";
        return;
    }

    set (Stage::WaitingForHost, "PromptMIDI " + release.version + " is ready. It installs as soon as you close " + hostText()
                                    + ", and is there the next time you open it.");
   #else
    juce::ignoreUnused (file);
   #endif
}

//==============================================================================
juce::String Updater::macChoices (const juce::StringArray& leaveOut)
{
    juce::String xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        << "<plist version=\"1.0\">\n<array>\n";

    for (auto& choice : leaveOut)
        xml << "  <dict>\n"
            << "    <key>choiceIdentifier</key><string>" << choice.retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789") << "</string>\n"
            << "    <key>choiceAttribute</key><string>selected</string>\n"
            << "    <key>attributeSetting</key><integer>0</integer>\n"
            << "  </dict>\n";

    xml << "</array>\n</plist>\n";
    return xml;
}

juce::StringArray Updater::macInstalledParts (const juce::File& root)
{
    juce::StringArray parts;

    if (root.getChildFile ("Library/Audio/Plug-Ins/VST3/PromptMIDI.vst3").isDirectory())       parts.add ("vst3");
    if (root.getChildFile ("Library/Audio/Plug-Ins/Components/PromptMIDI.component").isDirectory()) parts.add ("au");
    if (root.getChildFile ("Applications/PromptMIDI.app").isDirectory())                       parts.add ("app");
    if (root.getChildFile ("Library/Application Support/PromptMIDI/ReadMe.txt").existsAsFile()) parts.add ("readme");

    return parts;
}

void Updater::installMac (const juce::File& file)
{
   #if JUCE_MAC
    const auto parts = macInstalledParts();
    const auto path = context.runningBinary.getFullPathName();
    const bool standardPlace = path.startsWith ("/Library/Audio/Plug-Ins/") || path.startsWith ("/Applications/");

    if (parts.isEmpty() || ! standardPlace)
    {
        // Installed somewhere else (Customize > Location): the installer asks where, as the first time.
        file.startAsProcess();
        set (Stage::Done, "PromptMIDI is installed in a custom location, so the installer has opened: choose the same parts and locations as before.");
        return;
    }

    juce::StringArray leaveOut;

    for (auto* part : { "vst3", "au", "app", "readme" })
        if (! parts.contains (part))
            leaveOut.add (part);

    const auto choices = file.getSiblingFile ("choices.plist");
    choices.replaceWithText (macChoices (leaveOut));

    juce::StringArray command;
    const auto prompt = "PromptMIDI wants to install version " + release.version + ".";

    switch (context.elevation)
    {
        case Context::Elevation::Ask:
            command = { "/usr/bin/osascript",
                        "-e", "on run argv",
                        "-e", "do shell script \"/usr/sbin/installer -pkg \" & quoted form of (item 1 of argv) & \" -target / -applyChoiceChangesXML \" & quoted form of (item 2 of argv) with administrator privileges with prompt (item 3 of argv)",
                        "-e", "end run",
                        file.getFullPathName(), choices.getFullPathName(), prompt };
            break;
        case Context::Elevation::Sudo:
            command = { "/usr/bin/sudo", "-n", "/usr/sbin/installer", "-pkg", file.getFullPathName(), "-target", "/", "-applyChoiceChangesXML", choices.getFullPathName() };
            break;
        case Context::Elevation::None:
            command = { "/usr/sbin/installer", "-pkg", file.getFullPathName(), "-target", "/", "-applyChoiceChangesXML", choices.getFullPathName() };
            break;
    }

    juce::String output;

    if (! runChild (command, output, 15 * 60 * 1000))
    {
        fail (output.contains ("-128") || output.containsIgnoreCase ("cancel")
                  ? juce::String ("The update needs your password to install, so nothing changed. Press Update now to try again.")
                  : "The installer couldn't finish: " + output.trim().fromLastOccurrenceOf ("\n", false, false));
        return;
    }

    if (context.standalone && context.restartAfterInstall)
    {
        const auto problem = restartLater();
        const juce::ScopedLock sl (lock);
        state.stage = Stage::Done;
        state.restartApp = problem.isEmpty();
        state.message = problem.isEmpty() ? juce::String ("Restarting PromptMIDI to finish the update...") : problem;
        return;
    }

    set (Stage::Done, "PromptMIDI " + release.version + " is installed. Close and reopen " + hostText() + " to use it.");
   #else
    juce::ignoreUnused (file);
   #endif
}

//==============================================================================
juce::File Updater::linuxInstalledFiles (const juce::File& runningBinary)
{
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    const auto xdg = juce::SystemStats::getEnvironmentVariable ("XDG_DATA_HOME", {});
    const juce::File user = (xdg.isNotEmpty() ? juce::File (xdg) : home.getChildFile (".local/share")).getChildFile ("PromptMIDI/installed-files");
    const juce::File system ("/usr/local/share/PromptMIDI/installed-files");

    // The list that names where this binary lives...
    for (auto& list : { user, system })
    {
        if (! list.existsAsFile())
            continue;

        juce::StringArray lines;
        lines.addLines (list.loadFileAsString());

        for (auto& line : lines)
        {
            const juce::File entry (line.trim());

            if (line.trim().isNotEmpty() && juce::File::isAbsolutePath (line.trim())
                && (runningBinary == entry || runningBinary.isAChildOf (entry)))
                return list;
        }
    }

    // ... or else the one for this user, then the system's.
    return user.existsAsFile() ? user : (system.existsAsFile() ? system : juce::File());
}

Updater::LinuxInstall Updater::linuxInstallFor (const juce::StringArray& installedFiles, const juce::File& dataFolder)
{
    LinuxInstall r;
    juce::String vst3, vst2, app;

    for (auto& line : installedFiles)
    {
        const auto path = line.trim();

        if (path.isEmpty() || ! juce::File::isAbsolutePath (path))
            continue;

        const juce::File f (path);
        const auto name = f.getFileName();

        if (name == "PromptMIDI.vst3")    vst3 = f.getParentDirectory().getFullPathName();
        else if (name == "PromptMIDI.so") vst2 = f.getParentDirectory().getFullPathName();
        else if (name == "promptmidi")    app = f.getParentDirectory().getFullPathName();
    }

    if (vst3.isEmpty() && vst2.isEmpty() && app.isEmpty())
        return r;

    r.args.add ("--yes");

    if (vst3.isNotEmpty()) r.args.addArray ({ "--vst3-dir", vst3 }); else r.args.add ("--no-vst3");
    if (vst2.isNotEmpty()) r.args.addArray ({ "--vst2-dir", vst2 }); else r.args.add ("--no-vst2");
    if (app.isNotEmpty())  r.args.addArray ({ "--app-dir", app });   else r.args.add ("--no-app");

    if (! dataFolder.getChildFile ("ReadMe.txt").existsAsFile())
        r.args.add ("--no-readme");

    r.systemWide = ! dataFolder.isAChildOf (juce::File::getSpecialLocation (juce::File::userHomeDirectory));
    r.ok = true;
    return r;
}

void Updater::installLinux (const juce::File& file)
{
   #if JUCE_LINUX || JUCE_BSD
    const auto list = linuxInstalledFiles (context.runningBinary);

    if (! list.existsAsFile())
    {
        fail ("PromptMIDI wasn't installed with install.sh, so it can't update itself here. Get the new version from the website instead.");
        return;
    }

    juce::StringArray lines;
    lines.addLines (list.loadFileAsString());
    const auto plan = linuxInstallFor (lines, list.getParentDirectory());

    if (! plan.ok)
    {
        fail ("Couldn't tell which parts of PromptMIDI are installed. Get the new version from the website instead.");
        return;
    }

    const auto folder = file.getSiblingFile ("extract");
    folder.deleteRecursively();
    folder.createDirectory();
    juce::String output;

    if (! runChild ({ "tar", "-xzf", file.getFullPathName(), "-C", folder.getFullPathName() }, output, 5 * 60 * 1000))
    {
        fail ("Couldn't unpack the update: " + output.trim());
        return;
    }

    const auto script = folder.getChildFile ("PromptMIDI-Linux/install.sh");

    if (! script.existsAsFile())
    {
        fail ("The update doesn't contain install.sh.");
        return;
    }

    juce::StringArray command;

    if (plan.systemWide)
    {
        switch (context.elevation)
        {
            case Context::Elevation::Ask:
                if (! juce::File ("/usr/bin/pkexec").existsAsFile())
                {
                    fail ("PromptMIDI is installed for every user, which needs administrator rights. Run: sudo bash "
                          + script.getFullPathName() + " " + plan.args.joinIntoString (" "));
                    return;
                }

                command = { "/usr/bin/pkexec", "/bin/bash", script.getFullPathName() };
                break;
            case Context::Elevation::Sudo: command = { "/usr/bin/sudo", "-n", "/bin/bash", script.getFullPathName() }; break;
            case Context::Elevation::None: command = { "/bin/bash", script.getFullPathName() }; break;
        }
    }
    else
    {
        command = { "/bin/bash", script.getFullPathName() };
    }

    command.addArray (plan.args);

    if (! runChild (command, output, 10 * 60 * 1000))
    {
        fail (output.contains ("Not authorized") || output.contains ("dismissed")
                  ? juce::String ("The update needs administrator rights to install, so nothing changed. Press Update now to try again.")
                  : "The installer couldn't finish: " + output.trim().fromLastOccurrenceOf ("\n", false, false));
        return;
    }

    if (context.standalone && context.restartAfterInstall)
    {
        const auto problem = restartLater();
        const juce::ScopedLock sl (lock);
        state.stage = Stage::Done;
        state.restartApp = problem.isEmpty();
        state.message = problem.isEmpty() ? juce::String ("Restarting PromptMIDI to finish the update...") : problem;
        return;
    }

    set (Stage::Done, "PromptMIDI " + release.version + " is installed. Close and reopen " + hostText() + " to use it.");
   #else
    juce::ignoreUnused (file);
   #endif
}
