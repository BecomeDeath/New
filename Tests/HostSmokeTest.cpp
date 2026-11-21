// Loads the built PromptMIDI.vst3 the way a DAW would (through JUCE's VST3 hosting),
// restores a project state containing a composition, runs the transport, and checks
// that the plug-in emits MIDI notes and preview audio and saves its state again.
//
//   promptmidi_host_test path/to/PromptMIDI.vst3

#include <juce_audio_processors/juce_audio_processors.h>

#include "Composer.h"
#include "PromptInterpreter.h"
#include "SpecJson.h"

#include <cstdio>
#include <set>

static int failures = 0, checks = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++checks;                                                                     \
        if (! (cond)) {                                                               \
            ++failures;                                                               \
            std::printf ("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                             \
    } while (0)

struct TestPlayHead : public juce::AudioPlayHead
{
    double ppq = 0.0;
    double bpm = 120.0;
    bool playing = true;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        info.setPpqPosition (ppq);
        info.setBpm (bpm);
        info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        return info;
    }
};

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: promptmidi_host_test <PromptMIDI.vst3>\n");
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::String path = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]).getFullPathName();

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, path);

    std::printf ("scan\n");
    CHECK (found.size() == 1);

    if (found.isEmpty())
    {
        std::printf ("could not load %s\n", path.toRawUTF8());
        return 1;
    }

    const auto& desc = *found[0];
    CHECK (desc.name == "PromptMIDI");
    CHECK (desc.isInstrument);
    std::printf ("  %s by %s (%s), %d outputs\n", desc.name.toRawUTF8(), desc.manufacturerName.toRawUTF8(),
                 desc.category.toRawUTF8(), desc.numOutputChannels);

    const double sampleRate = 48000.0;
    const int blockSize = 512;
    juce::String error;
    auto plugin = format.createInstanceFromDescription (desc, sampleRate, blockSize, error);
    CHECK (plugin != nullptr);

    if (plugin == nullptr)
    {
        std::printf ("instantiation failed: %s\n", error.toRawUTF8());
        return 1;
    }

    std::printf ("  instantiated: %d in / %d out, accepts MIDI %d, produces MIDI %d\n", plugin->getTotalNumInputChannels(),
                 plugin->getTotalNumOutputChannels(), (int) plugin->acceptsMidi(), (int) plugin->producesMidi());
    CHECK (plugin->getTotalNumOutputChannels() == 2);
    CHECK (plugin->acceptsMidi() && plugin->producesMidi());

    std::printf ("state restore\n");
    const auto spec = pm::interpretPrompt ("Generate me a fantasy orchestral midi", 7);
    const auto comp = pm::compose (spec, 7);

    juce::XmlElement state ("PromptMIDI");
    state.setAttribute ("version", 1);
    state.setAttribute ("prompt", "Generate me a fantasy orchestral midi");
    state.setAttribute ("seed", "7");
    state.setAttribute ("spec", juce::JSON::toString (SpecJson::specToVar (spec), true));
    state.setAttribute ("composition", juce::JSON::toString (SpecJson::compositionToVar (comp), true));

    juce::MemoryBlock pluginState;
    juce::AudioProcessor::copyXmlToBinary (state, pluginState);

    juce::XmlElement wrapped ("VST3PluginState");
    wrapped.createNewChildElement ("IComponent")->addTextElement (pluginState.toBase64Encoding());
    juce::MemoryBlock hostState;
    juce::AudioProcessor::copyXmlToBinary (wrapped, hostState);
    plugin->setStateInformation (hostState.getData(), (int) hostState.getSize());

    std::printf ("playback\n");
    plugin->setPlayConfigDetails (0, 2, sampleRate, blockSize);
    plugin->prepareToPlay (sampleRate, blockSize);

    TestPlayHead playHead;
    plugin->setPlayHead (&playHead);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    int noteOns = 0, noteOffs = 0;
    float peak = 0.0f;
    std::set<int> channels;

    const int blocks = (int) (sampleRate * 20.0 / blockSize); // 20 seconds = 40 beats at 120 BPM

    for (int b = 0; b < blocks; ++b)
    {
        playHead.ppq = (double) b * blockSize / sampleRate * (playHead.bpm / 60.0);
        midi.clear();
        buffer.clear();
        plugin->processBlock (buffer, midi);

        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();

            if (m.isNoteOn())
            {
                ++noteOns;
                channels.insert (m.getChannel());
            }
            else if (m.isNoteOff())
            {
                ++noteOffs;
            }
        }

        peak = juce::jmax (peak, buffer.getMagnitude (0, 0, blockSize));
    }

    std::printf ("  %d note-ons, %d note-offs on %d channels, preview peak %.3f\n", noteOns, noteOffs, (int) channels.size(), peak);
    CHECK (noteOns > 100);
    CHECK (noteOffs > 100);
    CHECK (channels.size() >= 5);
    CHECK (peak > 0.01f && peak <= 1.0f);

    // Stopping the transport must silence everything.
    playHead.playing = false;
    midi.clear();
    plugin->processBlock (buffer, midi);
    int offsAfterStop = 0, onsAfterStop = 0;

    for (const auto meta : midi)
    {
        offsAfterStop += meta.getMessage().isNoteOff() ? 1 : 0;
        onsAfterStop += meta.getMessage().isNoteOn() ? 1 : 0;
    }

    CHECK (onsAfterStop == 0);
    std::printf ("  stop sent %d note-offs\n", offsAfterStop);

    std::printf ("state save\n");
    juce::MemoryBlock saved;
    plugin->getStateInformation (saved);
    auto savedXml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
    CHECK (savedXml != nullptr);

    if (savedXml != nullptr)
    {
        juce::MemoryBlock component;
        CHECK (component.fromBase64Encoding (savedXml->getChildElementAllSubText ("IComponent", {})));
        auto inner = juce::AudioProcessor::getXmlFromBinary (component.getData(), (int) component.getSize());
        CHECK (inner != nullptr && inner->hasTagName ("PromptMIDI"));

        if (inner != nullptr)
        {
            CHECK (inner->getStringAttribute ("composition").contains ("tracks"));
            CHECK (inner->getStringAttribute ("prompt") == "Generate me a fantasy orchestral midi");
            CHECK (! inner->getStringAttribute ("spec").contains ("apiKey"));
        }
    }

    plugin->releaseResources();
    plugin.reset();

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
