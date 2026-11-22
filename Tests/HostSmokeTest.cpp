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

    auto param = [&] (const char* name) -> juce::AudioProcessorParameter*
    {
        for (auto* prm : plugin->getParameters())
            if (prm->getName (64) == name)
                return prm;

        return nullptr;
    };

    struct Run { int ons = 0, offs = 0; std::set<int> channels; float peak = 0.0f; };

    auto play = [&] (double seconds, double fromPpq, bool bypassed)
    {
        Run r;
        const int blocks = (int) (sampleRate * seconds / blockSize);

        for (int b = 0; b < blocks; ++b)
        {
            playHead.ppq = fromPpq + (double) b * blockSize / sampleRate * (playHead.bpm / 60.0);
            midi.clear();
            buffer.clear();

            if (bypassed)
                plugin->processBlockBypassed (buffer, midi);
            else
                plugin->processBlock (buffer, midi);

            for (const auto meta : midi)
            {
                const auto m = meta.getMessage();

                if (m.isNoteOn())       { ++r.ons; r.channels.insert (m.getChannel()); }
                else if (m.isNoteOff()) { ++r.offs; }
            }

            r.peak = juce::jmax (r.peak, buffer.getMagnitude (0, 0, blockSize));
        }

        return r;
    };

    auto* hostMidi = param ("Send MIDI with Host");
    CHECK (hostMidi != nullptr && param ("Preview Sound") != nullptr && param ("Loop") != nullptr);

    // Defaults: while the DAW plays, PromptMIDI makes no sound and sends nothing, so a dragged-out
    // clip on another track is the only thing heard.
    const auto quiet = play (10.0, 0.0, false);
    std::printf ("  host playing, defaults: %d note-ons, peak %.4f\n", quiet.ons, quiet.peak);
    CHECK (quiet.ons == 0);
    CHECK (quiet.peak < 1.0e-9f);

    // Opt in to MIDI with the host: notes go out on their channels, still no audio.
    if (hostMidi != nullptr)
        hostMidi->setValueNotifyingHost (1.0f);

    const auto sent = play (20.0, 0.0, false);
    std::printf ("  send MIDI with host: %d note-ons, %d note-offs on %d channels, audio peak %.4f\n",
                 sent.ons, sent.offs, (int) sent.channels.size(), sent.peak);
    CHECK (sent.ons > 100);
    CHECK (sent.offs > 100);
    CHECK (sent.channels.size() >= 5);
    CHECK (sent.peak < 1.0e-9f);

    // Bypassing mid-piece ends every sounding note and stays silent.
    const auto bypass = play (2.0, 40.0, true);
    std::printf ("  bypassed: %d note-ons, %d note-offs, peak %.4f\n", bypass.ons, bypass.offs, bypass.peak);
    CHECK (bypass.ons == 0);
    CHECK (bypass.offs > 0);
    CHECK (bypass.peak < 1.0e-9f);

    // Stopping the transport must silence everything.
    play (1.0, 0.0, false);
    playHead.playing = false;
    const auto stopped = play (0.5, 0.0, false);
    CHECK (stopped.ons == 0 && stopped.peak < 1.0e-9f);
    std::printf ("  stop sent %d note-offs\n", stopped.offs);
    playHead.playing = true;

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

    std::printf ("loop region\n");
    {
        // A loopable cue: past its end, playback keeps cycling through the loop region (as a
        // game engine would), and the end stinger is not played; with Loop off it stops.
        const auto loopSpec = pm::interpretPrompt ("Epic orchestral battle loop for a boss fight", 3);
        const auto loopComp = pm::compose (loopSpec, 3);
        CHECK (loopComp.loopEnd > loopComp.loopStart && loopComp.loopEnd < loopComp.lengthTicks);

        juce::XmlElement loopState ("PromptMIDI");
        loopState.setAttribute ("version", 1);
        loopState.setAttribute ("prompt", "Epic orchestral battle loop for a boss fight");
        loopState.setAttribute ("spec", juce::JSON::toString (SpecJson::specToVar (loopSpec), true));
        loopState.setAttribute ("composition", juce::JSON::toString (SpecJson::compositionToVar (loopComp), true));
        juce::MemoryBlock inner;
        juce::AudioProcessor::copyXmlToBinary (loopState, inner);
        juce::XmlElement outer ("VST3PluginState");
        outer.createNewChildElement ("IComponent")->addTextElement (inner.toBase64Encoding());
        juce::MemoryBlock blob;
        juce::AudioProcessor::copyXmlToBinary (outer, blob);
        plugin->setStateInformation (blob.getData(), (int) blob.getSize());

        juce::AudioProcessorParameter* loopParam = nullptr;

        for (auto* prm : plugin->getParameters())
            if (prm->getName (32) == "Loop")
                loopParam = prm;

        CHECK (loopParam != nullptr);

        auto notesPastTheEnd = [&] (bool looping)
        {
            if (loopParam != nullptr)
                loopParam->setValueNotifyingHost (looping ? 1.0f : 0.0f);

            playHead.playing = true;
            playHead.bpm = loopComp.tempo;
            const double startPpq = (double) loopComp.lengthTicks / pm::kPpq + 37.0; // well past the end
            int ons = 0;

            for (int b = 0; b < (int) (sampleRate * 4.0 / blockSize); ++b)
            {
                playHead.ppq = startPpq + (double) b * blockSize / sampleRate * (playHead.bpm / 60.0);
                midi.clear();
                buffer.clear();
                plugin->processBlock (buffer, midi);

                for (const auto meta : midi)
                    ons += meta.getMessage().isNoteOn() ? 1 : 0;
            }

            return ons;
        };

        const int looped = notesPastTheEnd (true);
        const int stopped = notesPastTheEnd (false);
        std::printf ("  past the end: %d note-ons looping, %d without\n", looped, stopped);
        CHECK (looped > 20);
        CHECK (stopped == 0);
    }

    plugin->releaseResources();
    plugin.reset();

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
