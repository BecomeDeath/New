// Renders the plug-in editor offscreen to a PNG (used to check the UI on headless CI).
//
//   promptmidi_ui_snapshot out.png ["prompt"]

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "promptmidi.png");
    const juce::String prompt = argc > 2 ? juce::String (argv[2]) : juce::String ("Generate me a fantasy orchestral midi");
    const float scale = argc > 3 ? juce::String (argv[3]).getFloatValue() : 2.0f;

    PromptMidiProcessor processor;
    processor.seedLocked = true;
    processor.seed = 7;
    processor.generate (prompt);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    editor->setSize (1040, 720);

    // Deliver the (normally asynchronous) change message so the editor shows the result.
    processor.dispatchPendingMessages();

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat png;
    const bool ok = stream.openedOk() && png.writeImageToStream (image, stream);

    std::printf ("%s %s (%dx%d)\n", ok ? "wrote" : "FAILED to write", out.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
    editor.reset();
    return ok ? 0 : 1;
}
