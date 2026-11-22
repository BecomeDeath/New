#include "PreviewEngine.h"

#include "tsf.h"

#include <climits>
#include <cmath>

//==============================================================================
SoundBank::SoundBank() : juce::Thread ("PromptMIDI SoundFont loader") {}

SoundBank::~SoundBank()
{
    signalThreadShouldExit();
    notify();
    stopThread (15000);
}

void SoundBank::setSource (const juce::String& newSource)
{
    {
        const juce::ScopedLock sl (lock);

        if (newSource == source)
            return;

        source = newSource;
        loading = true;
        finished.reset();
        description = newSource == "sketch" ? juce::String ("Sketch synth") : juce::String ("Loading instruments...");
    }

    if (isThreadRunning())
        notify();
    else
        startThread (juce::Thread::Priority::low);
}

juce::String SoundBank::getSource() const
{
    const juce::ScopedLock sl (lock);
    return source;
}

std::shared_ptr<tsf> SoundBank::getBank() const
{
    const juce::ScopedLock sl (lock);
    return bank;
}

juce::String SoundBank::getDescription() const
{
    const juce::ScopedLock sl (lock);
    return description;
}

bool SoundBank::waitUntilLoaded (int timeoutMs)
{
    return finished.wait (timeoutMs);
}

juce::File SoundBank::userSoundFontFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("PromptMIDI").getChildFile ("Sounds");
}

juce::File SoundBank::findBuiltInSoundFont()
{
    const auto binary = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    const auto dir = binary.getParentDirectory();
    juce::Array<juce::File> places;

    places.add (dir);                                                            // next to the app or test tool
    places.add (dir.getSiblingFile ("Resources"));                               // a bundle: Contents/<arch>/binary
    places.add (dir.getChildFile ("PromptMIDI.vst3/Contents/Resources"));        // the app beside the VST3 (the download)

   #if JUCE_WINDOWS
    places.add (juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory)
                    .getChildFile ("Common Files/VST3/PromptMIDI.vst3/Contents/Resources"));
   #elif JUCE_MAC
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    places.add (home.getChildFile ("Library/Audio/Plug-Ins/VST3/PromptMIDI.vst3/Contents/Resources"));
    places.add (juce::File ("/Library/Audio/Plug-Ins/VST3/PromptMIDI.vst3/Contents/Resources"));
    places.add (home.getChildFile ("Library/Audio/Plug-Ins/Components/PromptMIDI.component/Contents/Resources"));
    places.add (juce::File ("/Library/Audio/Plug-Ins/Components/PromptMIDI.component/Contents/Resources"));
   #else
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    places.add (home.getChildFile (".vst3/PromptMIDI.vst3/Contents/Resources"));
    places.add (juce::File ("/usr/local/lib/vst3/PromptMIDI.vst3/Contents/Resources"));
    places.add (juce::File ("/usr/lib/vst3/PromptMIDI.vst3/Contents/Resources"));
   #endif

    places.add (userSoundFontFolder());

    for (auto& place : places)
    {
        const auto f = place.getChildFile (builtInFileName);

        if (f.existsAsFile())
            return f;
    }

    return {};
}

void SoundBank::run()
{
    while (! threadShouldExit())
    {
        juce::String wanted;

        {
            const juce::ScopedLock sl (lock);
            wanted = source;
        }

        std::shared_ptr<tsf> loaded;
        juce::String text;

        if (wanted == "sketch")
        {
            text = "Sketch synth";
        }
        else
        {
            const auto file = wanted.isEmpty() ? findBuiltInSoundFont() : juce::File (wanted);
            juce::MemoryBlock data;

            if (! file.existsAsFile())
                text = wanted.isEmpty() ? juce::String ("Sketch synth (the instrument sounds weren't found)")
                                        : "Sketch synth (couldn't find " + file.getFileName() + ")";
            else if (! file.loadFileAsData (data) || data.getSize() < 64 || data.getSize() > (size_t) INT_MAX)
                text = "Sketch synth (couldn't read " + file.getFileName() + ")";
            else if (auto* t = tsf_load_memory (data.getData(), (int) data.getSize()))
            {
                loaded = std::shared_ptr<tsf> (t, [] (tsf* p) { tsf_close (p); });
                text = wanted.isEmpty() ? juce::String ("GeneralUser GS") : file.getFileNameWithoutExtension();
            }
            else
            {
                text = "Sketch synth (" + file.getFileName() + " isn't a SoundFont 2 file)";
            }
        }

        std::shared_ptr<tsf> previous;

        {
            const juce::ScopedLock sl (lock);

            if (wanted != source)
                continue;                        // changed while loading: load the new one

            previous = std::move (bank);
            bank = std::move (loaded);
            description = text;
            ++generation;
            loading = false;
        }

        previous.reset();                         // (outside the lock)
        finished.signal();
        wait (-1);
    }
}

//==============================================================================
struct SampledSynth::Player
{
    std::shared_ptr<tsf> bank;   // keeps the shared samples alive for as long as this player
    tsf* synth = nullptr;

    ~Player()
    {
        if (synth != nullptr)
            tsf_close (synth);
    }
};

SampledSynth::SampledSynth()
{
    juce::Reverb::Parameters p;
    p.roomSize = 0.62f;
    p.damping = 0.5f;
    p.wetLevel = 0.17f;
    p.dryLevel = 0.9f;
    p.width = 1.0f;
    reverb.setParameters (p);
}

SampledSynth::~SampledSynth()
{
    delete active;
    delete pending.exchange (nullptr);
    delete retired.exchange (nullptr);
}

void SampledSynth::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    reverb.setSampleRate (sampleRate);
    build();
}

void SampledSynth::setBank (std::shared_ptr<tsf> newBank)
{
    currentBank = std::move (newBank);
    build();
}

void SampledSynth::build()
{
    auto* p = new Player();

    if (currentBank != nullptr)
    {
        p->bank = currentBank;
        p->synth = tsf_copy (currentBank.get());

        if (p->synth != nullptr)
        {
            // Everything the audio thread needs is allocated here: 16 channels, a fixed voice pool.
            tsf_set_output (p->synth, TSF_STEREO_INTERLEAVED, (int) sampleRate, -8.0f);
            tsf_set_max_voices (p->synth, 256);
            tsf_channel_set_presetnumber (p->synth, 15, 0, 0);

            for (int ch = 0; ch < 16; ++ch)
                tsf_channel_set_presetnumber (p->synth, ch, 0, ch == 9 ? 1 : 0);
        }
    }

    if (auto* old = pending.exchange (p))
        delete old;   // never reached the audio thread
}

void SampledSynth::collectGarbage()
{
    delete retired.exchange (nullptr);
}

void SampledSynth::beginBlock()
{
    // Take a new player only when the old one can be handed back for freeing.
    if (pending.load() == nullptr || retired.load() != nullptr)
        return;

    if (auto* p = pending.exchange (nullptr))
    {
        retired.store (active);
        active = p;
        reverb.reset();

        if (active->synth != nullptr)
            for (int ch = 0; ch < 16; ++ch)
                tsf_channel_set_presetnumber (active->synth, ch, programs[(size_t) ch], ch == 9 ? 1 : 0);
    }
}

bool SampledSynth::isActive() const
{
    return active != nullptr && active->synth != nullptr;
}

void SampledSynth::reset()
{
    if (isActive())
        for (int ch = 0; ch < 16; ++ch)
            tsf_channel_sounds_off_all (active->synth, ch);

    reverb.reset();
}

void SampledSynth::setProgram (int channel, int program)
{
    channel = juce::jlimit (0, 15, channel);
    programs[(size_t) channel] = juce::jlimit (0, 127, program);

    if (isActive())
        tsf_channel_set_presetnumber (active->synth, channel, programs[(size_t) channel], channel == 9 ? 1 : 0);
}

void SampledSynth::handle (const juce::MidiMessage& m)
{
    if (! isActive())
        return;

    auto* t = active->synth;
    const int ch = juce::jlimit (0, 15, m.getChannel() - 1);

    if (m.isNoteOn())
    {
        tsf_channel_note_on (t, ch, m.getNoteNumber(), m.getFloatVelocity());
    }
    else if (m.isNoteOff())
    {
        tsf_channel_note_off (t, ch, m.getNoteNumber());
    }
    else if (m.isProgramChange())
    {
        setProgram (ch, m.getProgramChangeNumber());
    }
    else if (m.isController())
    {
        const int cc = m.getControllerNumber();

        // Bank select would point the channel at banks this SoundFont may not have.
        if (cc != 0 && cc != 32)
            tsf_channel_midi_control (t, ch, cc, m.getControllerValue());
    }
}

void SampledSynth::render (juce::AudioBuffer<float>& buffer, int start, int num, float gain)
{
    if (! isActive() || num <= 0)
        return;

    auto* t = active->synth;
    auto* outL = buffer.getWritePointer (0, start);
    auto* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, start) : nullptr;

    // Gentle ceiling: an orchestra of General MIDI sounds can add up past full scale.
    auto soft = [] (float x)
    {
        const float a = std::abs (x);
        return a < 0.85f ? x : std::copysign (0.85f + 0.15f * std::tanh ((a - 0.85f) / 0.15f), x);
    };

    for (int done = 0; done < num;)
    {
        const int n = juce::jmin (chunk, num - done);
        tsf_render_float (t, interleaved.data(), n, 0);

        for (int i = 0; i < n; ++i)
        {
            left[(size_t) i] = interleaved[(size_t) (2 * i)];
            right[(size_t) i] = interleaved[(size_t) (2 * i + 1)];
        }

        reverb.processStereo (left.data(), right.data(), n);

        for (int i = 0; i < n; ++i)
        {
            const float l = soft (left[(size_t) i] * gain), r = soft (right[(size_t) i] * gain);

            if (outR != nullptr)
            {
                outL[done + i] += l;
                outR[done + i] += r;
            }
            else
            {
                outL[done + i] += 0.5f * (l + r);
            }
        }

        done += n;
    }
}

//==============================================================================
PreviewEngine::PreviewEngine() = default;

void PreviewEngine::prepare (double sampleRate)
{
    sketch.prepare (sampleRate);
    sampled.prepare (sampleRate);
    refresh();
}

void PreviewEngine::setSource (const juce::String& source)
{
    bank->setSource (source);
    refresh();
}

void PreviewEngine::refresh()
{
    sampled.collectGarbage();
    const int g = bank->getGeneration();

    if (g != shownGeneration)
    {
        shownGeneration = g;
        sampled.setBank (bank->getBank());
    }
}

juce::String PreviewEngine::getDescription() const
{
    return bank->getDescription();
}

void PreviewEngine::beginBlock()
{
    sampled.beginBlock();
    const bool now = sampled.isActive();

    if (now != sampledActive)
    {
        // Switching sounds mid-preview: the old one stops; the next notes use the new one.
        sketch.reset();
        sampled.reset();
        sampledActive = now;
        usingSampled.store (now);
    }
}

void PreviewEngine::reset()
{
    sketch.reset();
    sampled.reset();
}

void PreviewEngine::setProgram (int channel, int program)
{
    sketch.setProgram (channel, program);
    sampled.setProgram (channel, program);
}

void PreviewEngine::handle (const juce::MidiMessage& m)
{
    if (sampledActive)
        sampled.handle (m);
    else
        sketch.handle (m);
}

void PreviewEngine::render (juce::AudioBuffer<float>& buffer, int start, int num, float gain)
{
    if (sampledActive)
        sampled.render (buffer, start, num, gain);
    else
        sketch.render (buffer, start, num, gain);
}
