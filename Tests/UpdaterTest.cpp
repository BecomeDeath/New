// The in-app updater.
//
//   promptmidi_updater_test                                       unit tests (ctest)
//   promptmidi_updater_test install <installer> <installed binary> a real update with that installer
//                                                                 (CI: install, damage, update, check)

#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <juce_events/juce_events.h>

#include "UpdateCheck.h"
#include "Updater.h"

#include <cstdio>

static int failures = 0, checks = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++checks;                                                                     \
        if (! (cond)) {                                                               \
            ++failures;                                                               \
            std::printf ("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                             \
    } while (0)

/** Runs the updater until it stops working; returns its final state. */
static Updater::State runToEnd (Updater& u, int timeoutMs)
{
    const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

    while (u.isBusy() && juce::Time::getMillisecondCounter() < until)
        juce::Thread::sleep (50);

    return u.getState();
}

static updates::Release releaseFor (const juce::File& file, const juce::String& version)
{
    updates::Release r;
    r.version = version;
    r.download = juce::URL (file).toString (false);
    r.sha256 = juce::SHA256 (file).toHexString().toLowerCase();
    r.size = file.getSize();
    return r;
}

static int unitTests()
{
    const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("promptmidi-updater-test");
    temp.deleteRecursively();
    temp.createDirectory();

    std::printf ("the release manifest\n");
    {
        const juce::String sum ("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
        const auto json = R"({"version":"9.1.0","notes":"n","page":"https://example.com/p",
            "downloads":{"windows":"https://example.com/w.exe","macos":"https://example.com/m.pkg","linux":"https://example.com/l.tar.gz"},
            "sha256":{"windows":")" + sum + R"(","macos":"not a sum","linux":")" + sum.toUpperCase() + R"("},
            "sizes":{"windows":123456,"linux":789}})";

        const auto win = updates::parseManifest (json, "windows");
        CHECK (win.sha256 == sum && win.size == 123456 && win.canInstallInApp());
        CHECK (win.json == json);

        const auto mac = updates::parseManifest (json, "macos");
        CHECK (mac.sha256.isEmpty() && ! mac.canInstallInApp());   // a bad checksum: only the website

        const auto onLinux = updates::parseManifest (json, "linux");
        CHECK (onLinux.sha256 == sum && onLinux.size == 789);          // upper case is read too

        // A 1.1 manifest (no checksums): still offered, but through the website.
        const auto old = updates::parseManifest (R"({"version":"1.1.0","downloads":{"windows":"https://example.com/w.exe"}})", "windows");
        CHECK (old.isValid() && ! old.canInstallInApp());
    }

    std::printf ("windows: the install script\n");
    {
        const auto script = Updater::windowsScript ("C:\\Users\\O'Brien\\Temp\\PromptMIDI-Windows-Setup.exe", 4242,
                                                    "C:\\Temp\\install.log", "C:\\Program Files\\PromptMIDI\\PromptMIDI.exe");
        CHECK (script.contains ("Wait-Process -Id 4242"));
        CHECK (script.contains ("'C:\\Users\\O''Brien\\Temp\\PromptMIDI-Windows-Setup.exe'"));   // quotes escaped
        CHECK (script.contains ("/VERYSILENT") && script.contains ("/SUPPRESSMSGBOXES") && script.contains ("/NOCLOSEAPPLICATIONS"));
        CHECK (! script.contains ("/COMPONENTS") && ! script.contains ("/DIR"));   // the earlier choices are kept
        CHECK (script.contains ("explorer.exe") && script.contains ("PromptMIDI.exe"));

        const auto plugin = Updater::windowsScript ("C:\\Temp\\setup.exe", 0, "C:\\Temp\\install.log", {});
        CHECK (! plugin.contains ("Wait-Process") && ! plugin.contains ("explorer.exe"));
    }

    std::printf ("macos: keep the parts that are installed\n");
    {
        const auto root = temp.getChildFile ("root");
        root.getChildFile ("Library/Audio/Plug-Ins/VST3/PromptMIDI.vst3").createDirectory();
        root.getChildFile ("Library/Application Support/PromptMIDI").createDirectory();
        root.getChildFile ("Library/Application Support/PromptMIDI/ReadMe.txt").replaceWithText ("x");

        const auto parts = Updater::macInstalledParts (root);
        CHECK (parts.contains ("vst3") && parts.contains ("readme"));
        CHECK (! parts.contains ("au") && ! parts.contains ("app"));

        const auto choices = Updater::macChoices ({ "au", "app" });
        CHECK (choices.contains ("<string>au</string>") && choices.contains ("<string>app</string>"));
        CHECK (choices.contains ("<integer>0</integer>"));
        CHECK (! Updater::macChoices ({}).contains ("<dict>"));
    }

    std::printf ("linux: install.sh as it was installed\n");
    {
        const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        const auto data = home.getChildFile (".local/share/PromptMIDI-test-data");
        data.createDirectory();
        data.getChildFile ("ReadMe.txt").replaceWithText ("x");

        const juce::StringArray user { home.getChildFile ("plugins/vst3/PromptMIDI.vst3").getFullPathName(),
                                       home.getChildFile (".local/share/applications/promptmidi.desktop").getFullPathName() };
        const auto plan = Updater::linuxInstallFor (user, data);
        CHECK (plan.ok && ! plan.systemWide);
        CHECK (plan.args.contains ("--yes"));
        CHECK (plan.args.indexOf ("--vst3-dir") >= 0 && plan.args[plan.args.indexOf ("--vst3-dir") + 1] == home.getChildFile ("plugins/vst3").getFullPathName());
        CHECK (plan.args.contains ("--no-app") && plan.args.contains ("--no-vst2") && ! plan.args.contains ("--no-readme"));
        data.deleteRecursively();

        const juce::StringArray system { "/opt/x/vst3/PromptMIDI.vst3", "/usr/local/bin/promptmidi" };
        const auto everyone = Updater::linuxInstallFor (system, juce::File ("/usr/local/share/PromptMIDI"));
        CHECK (everyone.ok && everyone.systemWide);
        CHECK (everyone.args.contains ("--app-dir") && everyone.args.contains ("/usr/local/bin") && everyone.args.contains ("--no-readme"));

        CHECK (! Updater::linuxInstallFor ({ "/somewhere/else.txt" }, data).ok);
    }

    std::printf ("download and verify\n");
    {
        // A stand-in installer, served from a file:// URL.
        const auto installer = temp.getChildFile ("PromptMIDI-Test-Setup.bin");
        juce::MemoryBlock bytes (3 * 1024 * 1024);
        juce::Random random (7);
        auto* data = static_cast<juce::uint8*> (bytes.getData());

        for (size_t i = 0; i < bytes.getSize(); ++i)
            data[i] = (juce::uint8) random.nextInt (256);

        installer.replaceWithData (bytes.getData(), bytes.getSize());

        Updater u;
        u.context.downloadFolder = temp.getChildFile ("downloads");
        u.context.dryRun = true;

        auto good = releaseFor (installer, "9.2.0");
        u.start (good);
        auto s = runToEnd (u, 20000);
        CHECK (s.stage == Updater::Stage::Done);
        CHECK (s.downloaded == (juce::int64) bytes.getSize() && s.total == (juce::int64) bytes.getSize());
        CHECK (u.context.downloadFolder.getChildFile ("9.2.0/PromptMIDI-Test-Setup.bin").getSize() == (juce::int64) bytes.getSize());

        // Only what matches the published checksum is ever installed.
        u.reset();
        auto tampered = good;
        tampered.version = "9.3.0";
        tampered.sha256 = juce::String::repeatedString ("0", 64);
        u.start (tampered);
        s = runToEnd (u, 20000);
        CHECK (s.stage == Updater::Stage::Failed && s.message.contains ("checksum"));
        CHECK (! u.context.downloadFolder.getChildFile ("9.3.0/PromptMIDI-Test-Setup.bin").existsAsFile());

        u.reset();
        auto wrongSize = good;
        wrongSize.version = "9.4.0";
        wrongSize.size += 1;
        u.start (wrongSize);
        s = runToEnd (u, 20000);
        CHECK (s.stage == Updater::Stage::Failed);

        u.reset();
        auto missing = good;
        missing.version = "9.5.0";
        missing.download = juce::URL (temp.getChildFile ("not-there.bin")).toString (false);
        u.start (missing);
        s = runToEnd (u, 20000);
        CHECK (s.stage == Updater::Stage::Failed);

        // The standalone app restarts; inside a DAW it waits for the DAW (Windows) or says to reopen it.
        CHECK (u.getState().restartApp == false);
    }

    temp.deleteRecursively();
    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

/** A real update: what CI does after installing PromptMIDI with custom choices and damaging it. */
static int realUpdate (const juce::File& installer, const juce::File& installedBinary)
{
    Updater u;
    u.context.standalone = false;
    u.context.hostName = "the test";
    u.context.hostProcessId = 0;   // nothing to wait for
    u.context.runningBinary = installedBinary;
    u.context.downloadFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("promptmidi-updater-ci");
   #if JUCE_MAC
    u.context.elevation = Updater::Context::Elevation::Sudo;
   #else
    u.context.elevation = Updater::Context::Elevation::None;
   #endif

    u.start (releaseFor (installer, "99.0.0"));
    auto s = runToEnd (u, 15 * 60 * 1000);
    std::printf ("stage %d: %s\n", (int) s.stage, s.message.toRawUTF8());

   #if JUCE_WINDOWS
    // The installer runs in the background once nothing holds the files: wait for its exit code.
    if (s.stage == Updater::Stage::WaitingForHost)
    {
        const auto exitFile = u.context.downloadFolder.getChildFile ("99.0.0/install.log.exit");

        for (int i = 0; i < 600 && ! exitFile.existsAsFile(); ++i)
            juce::Thread::sleep (500);

        juce::Thread::sleep (500);
        const auto code = exitFile.loadFileAsString().trim();
        std::printf ("installer exit code: %s\n", code.toRawUTF8());
        return code == "0" ? 0 : 1;
    }
   #endif

    return s.stage == Updater::Stage::Done ? 0 : 1;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc >= 4 && juce::String (argv[1]) == "install")
        return realUpdate (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]),
                           juce::File::getCurrentWorkingDirectory().getChildFile (argv[3]));

    return unitTests();
}
