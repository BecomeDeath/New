// Renders PromptMIDI's in-plugin preview to a WAV file, exactly as the Play button plays it
// (used to check the preview's instruments and levels without an audio device).
//
//   promptmidi_render_preview out.wav ["prompt"] [seconds] [seed] [sketch]

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "preview.wav");
    const juce::String prompt = argc > 2 ? juce::String (argv[2]) : juce::String ("Generate me a fantasy orchestral midi");
    const double seconds = argc > 3 ? juce::String (argv[3]).getDoubleValue() : 30.0;
    const bool sketch = argc > 5 && juce::String (argv[5]) == "sketch";

    PromptMidiProcessor processor;
    processor.seedLocked = true;
    processor.seed = argc > 4 ? (uint64_t) juce::String (argv[4]).getLargeIntValue() : 7;
    processor.getPreviewEngine().setSource (sketch ? "sketch" : "");
    processor.generate (prompt);
    processor.getPreviewEngine().getBank().waitUntilLoaded (20000);

    const double sampleRate = 48000.0;
    const int block = 512;
    processor.prepareToPlay (sampleRate, block);
    processor.getPreviewEngine().refresh();

    const int total = (int) (seconds * sampleRate);
    juce::AudioBuffer<float> result (2, total);
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;

    processor.setPreviewPlaying (true, 0.0);

    for (int pos = 0; pos < total; pos += block)
    {
        midi.clear();
        processor.processBlock (buffer, midi);
        const int n = juce::jmin (block, total - pos);

        for (int ch = 0; ch < 2; ++ch)
            result.copyFrom (ch, pos, buffer, ch, 0, n);
    }

    double sum = 0.0;

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < total; ++i)
            sum += (double) result.getSample (ch, i) * result.getSample (ch, i);

    const float peak = result.getMagnitude (0, total);
    const double rms = std::sqrt (sum / juce::jmax (1, 2 * total));

    out.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (out.createOutputStream().release());
    bool ok = false;

    if (stream != nullptr)
    {
        const auto options = juce::AudioFormatWriterOptions().withSampleRate (sampleRate).withNumChannels (2).withBitsPerSample (16);

        if (auto writer = wav.createWriterFor (stream, options))
            ok = writer->writeFromAudioSampleBuffer (result, 0, total);
    }

    std::printf ("%s %s: %.1f s, sound \"%s\" (%s), peak %.3f (%.1f dBFS), rms %.1f dBFS\n",
                 ok ? "wrote" : "FAILED to write", out.getFullPathName().toRawUTF8(), seconds,
                 processor.getPreviewSoundDescription().toRawUTF8(), processor.isPreviewSampled() ? "sampled" : "sketch",
                 peak, 20.0 * std::log10 (juce::jmax (1.0e-9f, peak)), 20.0 * std::log10 (juce::jmax (1.0e-12, rms)));
    processor.releaseResources();
    return ok ? 0 : 1;
}
