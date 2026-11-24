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
