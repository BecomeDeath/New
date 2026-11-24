// Renders the plug-in editor offscreen to a PNG (used to check the UI on headless CI).
//
//   promptmidi_ui_snapshot out.png ["prompt"] [scale] [width] [height] [track to edit, -1 = none] [zoom] [variations]
//
// The piece is thought through (Thorough). With PROMPTMIDI_SNAPSHOT_THINKING=<seconds> it then
// starts thinking about the prompt again (Deep) and takes the picture that far in.

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Timeline.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "promptmidi.png");
    const juce::String prompt = argc > 2 ? juce::String (argv[2]) : juce::String ("Generate me a fantasy orchestral midi");
    const float scale = argc > 3 ? juce::String (argv[3]).getFloatValue() : 2.0f;

    PromptMidiProcessor processor;
    processor.seedLocked = true;
    processor.seed = 7;
    processor.thinkInBackground = false;
    processor.depthOverride = 1;
    processor.generate (prompt);

    for (auto& line : processor.getThoughtReasoning())
        std::printf ("  %s\n", line.toRawUTF8());

    for (int i = 0; i < (argc > 8 ? juce::String (argv[8]).getIntValue() : 0); ++i)
        processor.makeVariation();   // shows the take history

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    editor->setSize (argc > 4 ? juce::String (argv[4]).getIntValue() : 1240, argc > 5 ? juce::String (argv[5]).getIntValue() : 800);

    // Deliver the (normally asynchronous) change message so the editor shows the result.
    processor.dispatchPendingMessages();

    if (auto* arrangement = dynamic_cast<Arrangement*> (editor->findChildWithID ("arrangement")))
    {
        if (argc > 6 && juce::String (argv[6]).getIntValue() >= 0)
            arrangement->selectTrack (juce::String (argv[6]).getIntValue());

        if (argc > 7)
            arrangement->zoomBy (juce::String (argv[7]).getDoubleValue(), 0.0f);
    }

    if (const auto thinking = juce::SystemStats::getEnvironmentVariable ("PROMPTMIDI_SNAPSHOT_THINKING", {}); thinking.isNotEmpty())
    {
        processor.thinkInBackground = true;
        processor.depthOverride = 2;
        processor.generate (prompt);
        juce::MessageManager::getInstance()->runDispatchLoopUntil ((int) (thinking.getDoubleValue() * 1000.0));
    }

    // PROMPTMIDI_SNAPSHOT_UPDATE=offer | download | ready | failed: the update dialog in that state.
    if (const auto update = juce::SystemStats::getEnvironmentVariable ("PROMPTMIDI_SNAPSHOT_UPDATE", {}); update.isNotEmpty())
    {
        if (auto* dialog = dynamic_cast<UpdateDialog*> (editor->findChildWithID ("updateDialog")))
        {
            updates::Release r;
            r.version = "1.2.0";
            r.notes = "Updates now install from inside PromptMIDI: no browser, no installer to run. The update prompt appears in the middle of the window.";
            r.download = "https://example.com/PromptMIDI-Windows-Setup.exe";
            r.sha256 = juce::String::repeatedString ("a", 64);
            dialog->offer (r, true, "Installs right here, keeping the parts and folders you chose. It downloads now and installs when you close Ableton Live; Windows asks for permission once.");

            Updater::State s;
            s.version = "1.2.0";

            if (update == "download")
            {
                s.stage = Updater::Stage::Downloading;
                s.downloaded = 14 * 1024 * 1024;
                s.total = 36 * 1024 * 1024;
                dialog->follow (s);
            }
            else if (update == "ready")
            {
                s.stage = Updater::Stage::WaitingForHost;
                s.message = "PromptMIDI 1.2.0 is ready. It installs as soon as you close Ableton Live, and is there the next time you open it.";
                dialog->follow (s);
            }
            else if (update == "failed")
            {
                s.stage = Updater::Stage::Failed;
                s.message = "Couldn't download the update (no connection). Check your internet connection and try again.";
                dialog->follow (s);
            }

            dialog->setVisible (true);
        }
    }

    if (juce::SystemStats::getEnvironmentVariable ("PROMPTMIDI_SNAPSHOT_SETTINGS", {}).isNotEmpty())
    {
        // The Settings panel on its own.
        SettingsPanel panel (*processor.settings, {});
        const auto image = panel.createComponentSnapshot (panel.getLocalBounds(), true, scale);
        out.deleteFile();
        juce::FileOutputStream stream (out);
        const bool ok = stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream);
        std::printf ("%s %s\n", ok ? "wrote" : "FAILED to write", out.getFullPathName().toRawUTF8());
        editor.reset();
        return ok ? 0 : 1;
    }

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat png;
    const bool ok = stream.openedOk() && png.writeImageToStream (image, stream);

    std::printf ("%s %s (%dx%d)\n", ok ? "wrote" : "FAILED to write", out.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
    editor.reset();
    return ok ? 0 : 1;
}
