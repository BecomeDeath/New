#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Composer.h"
#include "MidiFileWriter.h"
#include "SpecJson.h"

namespace
{
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        return {
            std::make_unique<AudioParameterBool>  (ParameterID { "previewSound", 1 }, "Preview Sound", true),
            std::make_unique<AudioParameterFloat> (ParameterID { "previewVolume", 1 }, "Preview Volume", NormalisableRange<float> (0.0f, 1.0f), 0.7f),
            std::make_unique<AudioParameterBool>  (ParameterID { "midiOut", 1 }, "MIDI Output", true),
            std::make_unique<AudioParameterBool>  (ParameterID { "syncHost", 1 }, "Sync to Host", true),
            std::make_unique<AudioParameterBool>  (ParameterID { "loop", 1 }, "Loop", true),
        };
    }

    juce::String fromStd (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
}

PromptMidiProcessor::PromptMidiProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMS", createLayout())
{
    previewSoundParam  = parameters.getRawParameterValue ("previewSound");
    previewVolumeParam = parameters.getRawParameterValue ("previewVolume");
    midiOutParam       = parameters.getRawParameterValue ("midiOut");
    syncHostParam      = parameters.getRawParameterValue ("syncHost");
    loopParam          = parameters.getRawParameterValue ("loop");

    generatedMidi.ensureSize (8192);
    statusMessage = "Describe the music you want, then press Generate.";
    startTimer (500);
}

PromptMidiProcessor::~PromptMidiProcessor()
{
    *aliveToken = false;
    stopTimer();
    claudeJob.reset();
}

//==============================================================================
bool PromptMidiProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void PromptMidiProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    synth.prepare (currentSampleRate);
    wasPlaying = false;
    expectedTick = -1.0;

    for (auto& ch : activeNotes)
        ch.fill (0);
}

void PromptMidiProcessor::allNotesOff (int samplePosition)
{
    for (int ch = 0; ch < 16; ++ch)
    {
        for (int note = 0; note < 128; ++note)
        {
            if (activeNotes[(size_t) ch][(size_t) note] > 0)
            {
                generatedMidi.addEvent (juce::MidiMessage::noteOff (ch + 1, note), samplePosition);
                activeNotes[(size_t) ch][(size_t) note] = 0;
            }
        }
    }
}

void PromptMidiProcessor::emitRange (const PlaybackData& data, double fromTick, double toTick, bool inclusiveEnd,
                                     double ticksPerSample, int firstSample, int numSamples)
{
    const auto& ev = data.events;
    auto it = std::lower_bound (ev.begin(), ev.end(), fromTick,
                                [] (const PlaybackEvent& e, double t) { return (double) e.tick < t; });

    const uint32_t muted = mutedMask.load (std::memory_order_relaxed);

    for (; it != ev.end(); ++it)
    {
        const double t = (double) it->tick;

        if (t > toTick || (! inclusiveEnd && t >= toTick))
            break;

        const int sample = juce::jlimit (0, numSamples - 1, firstSample + (int) ((t - fromTick) / ticksPerSample));
        const uint8_t type = it->bytes[0] & 0xF0;
        const int ch = it->bytes[0] & 0x0F;

        if (type == 0x90)
        {
            if ((muted >> it->track) & 1u)
                continue;

            auto& count = activeNotes[(size_t) ch][(size_t) (it->bytes[1] & 0x7F)];

            if (count < 255)
                ++count;
        }
        else if (type == 0x80)
        {
            auto& count = activeNotes[(size_t) ch][(size_t) (it->bytes[1] & 0x7F)];

            if (count == 0)
                continue; // never started (muted, or playback began after its note-on)

            --count;
        }
        else if (type == 0xB0 && ((muted >> it->track) & 1u))
        {
            continue;
        }

        generatedMidi.addEvent (it->bytes, it->size, sample);
    }
}

void PromptMidiProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    generatedMidi.clear();

    if (numSamples <= 0)
        return;

    // Pick up a freshly generated composition.
    {
        const juce::SpinLock::ScopedTryLockType lock (playbackLock);

        if (lock.isLocked() && pendingPlayback != nullptr)
        {
            allNotesOff (0);
            activePlayback = std::move (pendingPlayback);
            pendingPlayback = nullptr;

            for (int ch = 0; ch < 16; ++ch)
                synth.setProgram (ch, activePlayback->programs[(size_t) ch]);
        }
    }

    const auto* data = activePlayback.get();
    const bool loop = loopParam->load() > 0.5f;

    bool hostPlaying = false;
    double hostPpq = 0.0, hostBpm = 120.0;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            hostPlaying = pos->getIsPlaying();

            if (auto ppq = pos->getPpqPosition())  hostPpq = *ppq;
            else                                   hostPlaying = false;

            if (auto bpm = pos->getBpm())
                hostBpm = juce::jlimit (10.0, 999.0, *bpm);
        }
    }

    bool playing = false;
    double startTick = 0.0, ticksPerSample = 0.0;
    const bool followHost = syncHostParam->load() > 0.5f && hostPlaying;

    if (data != nullptr && data->lengthTicks > 0)
    {
        if (followHost)
        {
            playing = true;
            ticksPerSample = hostBpm / 60.0 * pm::kPpq / currentSampleRate;
            startTick = hostPpq * pm::kPpq;
        }
        else if (previewPlaying.load())
        {
            if (previewRestart.exchange (false))
            {
                internalTick = 0.0;
                expectedTick = -1.0;
                allNotesOff (0);
            }

            playing = true;
            ticksPerSample = data->tempo / 60.0 * pm::kPpq / currentSampleRate;
            startTick = internalTick;
            internalTick += ticksPerSample * numSamples;
        }
    }

    if (! playing)
    {
        if (wasPlaying)
            allNotesOff (0);

        wasPlaying = false;
        expectedTick = -1.0;
        playheadTick.store (-1.0);
    }
    else
    {
        const double span = ticksPerSample * numSamples;

        // A jump in the host position (or the very first block) silences hanging notes.
        if (wasPlaying && std::abs (startTick - expectedTick) > ticksPerSample * 4.0 + 1.0)
            allNotesOff (0);

        expectedTick = startTick + span;
        wasPlaying = true;

        const double length = data->lengthTicks;

        if (loop)
        {
            double a = std::fmod (startTick, length);

            if (a < 0.0)
                a += length;

            const double b = a + span;

            if (b <= length)
            {
                emitRange (*data, a, b, false, ticksPerSample, 0, numSamples);
            }
            else
            {
                emitRange (*data, a, length, true, ticksPerSample, 0, numSamples);
                const int wrapSample = (int) ((length - a) / ticksPerSample);
                emitRange (*data, 0.0, b - length, false, ticksPerSample, wrapSample, numSamples);
            }

            playheadTick.store (b > length ? b - length : b);
        }
        else if (startTick < length + 1.0)
        {
            emitRange (*data, startTick, std::min (startTick + span, length), startTick + span >= length,
                       ticksPerSample, 0, numSamples);
            playheadTick.store (startTick);
        }
        else
        {
            allNotesOff (0);
            playheadTick.store (-1.0);

            if (! followHost)
                previewPlaying.store (false); // the one-shot preview reached the end
        }
    }

    // Preview synth
    if (previewSoundParam->load() > 0.5f)
    {
        const float gain = previewVolumeParam->load();
        int pos = 0;

        for (const auto meta : generatedMidi)
        {
            const int s = juce::jlimit (0, numSamples, meta.samplePosition);

            if (s > pos)
            {
                synth.render (buffer, pos, s - pos, gain);
                pos = s;
            }

            synth.handle (meta.getMessage());
        }

        if (pos < numSamples)
            synth.render (buffer, pos, numSamples - pos, gain);

        synthHadSound = true;
    }
    else if (synthHadSound)
    {
        synth.reset();
        synthHadSound = false;
    }

    // Incoming MIDI passes through; the generated part is added on top.
    if (midiOutParam->load() > 0.5f && ! generatedMidi.isEmpty())
        midi.addEvents (generatedMidi, 0, numSamples, 0);
}

//==============================================================================
uint64_t PromptMidiProcessor::randomSeed()
{
    return (uint64_t) juce::Random::getSystemRandom().nextInt (999999998) + 1;
}

pm::PromptOverrides PromptMidiProcessor::promptOverrides() const
{
    pm::PromptOverrides o;
    o.keyRoot = controls.keyRoot;
    o.mode = controls.mode;
    o.tempo = controls.tempo;
    o.bars = controls.bars;
    return o;
}

void PromptMidiProcessor::applyControls (pm::StyleSpec& s, bool includeMode) const
{
    if (controls.keyRoot >= 0)               s.keyRoot = controls.keyRoot;
    if (controls.mode >= 0 && includeMode)   s.mode = (pm::Mode) controls.mode;
    if (controls.tempo > 0)                  s.tempo = controls.tempo;

    if (controls.bars > 0 && controls.bars != s.bars)
    {
        // Keep the arrangement's shape: parts that came in a quarter of the way through still do.
        for (auto& t : s.tracks)
            t.entryBar = controls.bars < 8 ? 0 : (int) std::lround ((double) t.entryBar * controls.bars / juce::jmax (1, s.bars));

        s.bars = controls.bars;
    }

    pm::normalizeSpec (s);
}

juce::String PromptMidiProcessor::controlsDescription() const
{
    juce::StringArray parts;

    if (controls.keyRoot >= 0)  parts.add ("key " + SpecJson::keyName (controls.keyRoot));
    if (controls.mode >= 0)     parts.add ("mode " + juce::String (pm::modeInfo ((pm::Mode) controls.mode).id));
    if (controls.tempo > 0)     parts.add ("tempo " + juce::String (controls.tempo) + " BPM");
    if (controls.bars > 0)      parts.add (juce::String (controls.bars) + " bars");

    return parts.joinIntoString (", ");
}

void PromptMidiProcessor::setStatus (Status s, const juce::String& message)
{
    status = s;
    statusMessage = message;
    sendChangeMessage();
}

void PromptMidiProcessor::cancelGeneration()
{
    if (claudeJob != nullptr)
    {
        ++requestCounter;
        claudeJob.reset();
        setStatus (Status::Idle, "Cancelled.");
    }
}

void PromptMidiProcessor::generate (const juce::String& newPrompt)
{
    prompt = newPrompt.trim();

    if (prompt.isEmpty())
    {
        setStatus (Status::Error, "Type a description of the music first, e.g. \"Make me a soft piano midi\".");
        return;
    }

    if (claudeJob != nullptr)
    {
        ++requestCounter;
        claudeJob.reset();
    }

    const uint64_t newSeed = seedLocked ? seed : randomSeed();

    if (engine == Engine::Offline)
    {
        lastPrompt = prompt;
        lastEngine = Engine::Offline;
        interpretSeed = newSeed;
        baseSpec = pm::interpretPrompt (prompt.toStdString(), newSeed, promptOverrides());
        mutedMask.store (0);
        applySpec (baseSpec, newSeed);
        setStatus (Status::Idle, "Generated offline. Press Variation for another take, or drag the MIDI into your DAW.");
        return;
    }

    if (! settings->hasApiKey())
    {
        setStatus (Status::Error, "Add your Anthropic API key in Settings to compose with Claude, or switch to Offline.");
        return;
    }

    Claude::Options options;
    options.apiKey = settings->getApiKey();
    options.model = settings->getModel();
    options.effort = settings->getEffort();

    const int requestId = ++requestCounter;
    setStatus (Status::Working, "Composing with Claude...");

    claudeJob = std::make_unique<Claude::Job> (prompt, controlsDescription(), options,
        [this, requestId, newSeed, requestPrompt = prompt] (const Claude::Result& result)
        {
            if (requestId != requestCounter)
                return;

            claudeJob.reset();

            if (! result.ok)
            {
                setStatus (Status::Error, result.error);
                return;
            }

            baseSpec = result.spec;
            baseSpec.interpretation.insert (baseSpec.interpretation.begin(),
                                            "Planned by " + (result.model.isNotEmpty() ? result.model : settings->getModel()).toStdString());
            lastPrompt = requestPrompt;
            lastEngine = Engine::Claude;
            interpretSeed = newSeed;

            auto s = baseSpec;
            applyControls (s, true);
            mutedMask.store (0);
            applySpec (s, newSeed);
            setStatus (Status::Idle, "Composed with Claude. Press Variation for another take with the same plan.");
        });
}

void PromptMidiProcessor::makeVariation()
{
    if (claudeJob != nullptr)
        return;

    if (spec.tracks.empty())
    {
        generate (prompt);
        return;
    }

    applySpec (spec, randomSeed());
    setStatus (Status::Idle, "New variation of the same plan.");
}

void PromptMidiProcessor::controlsChanged()
{
    if (claudeJob != nullptr || spec.tracks.empty())
        return;

    if (lastEngine == Engine::Offline)
    {
        // Same prompt, same seed, new controls: only what the controls touch changes.
        applySpec (pm::interpretPrompt (lastPrompt.toStdString(), interpretSeed, promptOverrides()), seed);
        setStatus (Status::Idle, "Updated.");
        return;
    }

    // Claude's harmony was written for its scale, so a new scale needs a new plan.
    auto s = baseSpec;
    applyControls (s, false);
    applySpec (s, seed);

    const bool modeDiffers = controls.mode >= 0 && (pm::Mode) controls.mode != baseSpec.mode;
    setStatus (Status::Idle, modeDiffers ? "Updated. Press Generate to have Claude re-plan in the new scale."
                                         : "Updated.");
}

void PromptMidiProcessor::applySpec (const pm::StyleSpec& newSpec, uint64_t newSeed)
{
    spec = newSpec;
    seed = newSeed;
    composition = pm::compose (spec, seed);
    publishPlayback();
}

void PromptMidiProcessor::publishPlayback()
{
    auto data = std::make_shared<PlaybackData>();
    data->lengthTicks = composition.lengthTicks;
    data->tempo = composition.tempo;
    data->programs.fill (0);

    for (int ti = 0; ti < (int) composition.tracks.size() && ti < 32; ++ti)
    {
        const auto& t = composition.tracks[(size_t) ti];
        const uint8_t ch = (uint8_t) (t.channel & 0x0F);

        if (! t.isDrum)
            data->programs[ch] = t.program;

        for (auto& c : t.controls)
            data->events.push_back ({ c.tick, 1, 3, (int8_t) ti, { (uint8_t) (0xB0 | ch), (uint8_t) c.controller, (uint8_t) c.value } });

        for (auto& n : t.notes)
        {
            data->events.push_back ({ n.start, 2, 3, (int8_t) ti, { (uint8_t) (0x90 | ch), (uint8_t) n.pitch, (uint8_t) n.velocity } });
            data->events.push_back ({ n.start + n.length, 0, 3, (int8_t) ti, { (uint8_t) (0x80 | ch), (uint8_t) n.pitch, 64 } });
        }
    }

    std::stable_sort (data->events.begin(), data->events.end(), [] (const PlaybackEvent& a, const PlaybackEvent& b)
                      { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });

    std::shared_ptr<const PlaybackData> published = data;

    {
        const juce::SpinLock::ScopedLockType lock (playbackLock);
        pendingPlayback = published;
    }

    retired.push_back (published);
    ++compositionVersion;
    sendChangeMessage();
}

void PromptMidiProcessor::timerCallback()
{
    // Free playback data the audio thread no longer references.
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [] (const std::shared_ptr<const PlaybackData>& p) { return p.use_count() == 1; }),
                   retired.end());
}

//==============================================================================
void PromptMidiProcessor::setTrackMuted (int index, bool muted)
{
    if (index < 0 || index >= 32)
        return;

    const uint32_t bit = 1u << index;
    auto mask = mutedMask.load();
    mutedMask.store (muted ? (mask | bit) : (mask & ~bit));
    sendChangeMessage();
}

bool PromptMidiProcessor::isTrackMuted (int index) const
{
    return index >= 0 && index < 32 && ((mutedMask.load() >> index) & 1u);
}

void PromptMidiProcessor::setPreviewPlaying (bool shouldPlay)
{
    if (shouldPlay)
        previewRestart.store (true);

    previewPlaying.store (shouldPlay);
}

juce::String PromptMidiProcessor::suggestedFileName (int trackIndex) const
{
    juce::String name = fromStd (composition.title).trim();

    if (name.isEmpty())
        name = "PromptMIDI";

    if (trackIndex >= 0 && trackIndex < (int) composition.tracks.size())
        name << " - " << fromStd (composition.tracks[(size_t) trackIndex].name);

    return juce::File::createLegalFileName (name + ".mid");
}

bool PromptMidiProcessor::writeMidiFile (const juce::File& file, int trackIndex) const
{
    if (! hasComposition())
        return false;

    // Muted tracks are left out of the full export.
    pm::Composition toWrite = composition;

    if (trackIndex < 0)
    {
        std::vector<pm::Track> kept;

        for (int i = 0; i < (int) toWrite.tracks.size(); ++i)
            if (! isTrackMuted (i))
                kept.push_back (toWrite.tracks[(size_t) i]);

        if (! kept.empty())
            toWrite.tracks = kept;
    }

    const auto bytes = pm::writeMidiFile (toWrite, trackIndex);
    return file.replaceWithData (bytes.data(), bytes.size());
}

juce::File PromptMidiProcessor::writeMidiToTempFile (int trackIndex) const
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PromptMIDI");
    dir.createDirectory();
    auto file = dir.getChildFile (suggestedFileName (trackIndex));
    writeMidiFile (file, trackIndex);
    return file;
}

//==============================================================================
void PromptMidiProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("PromptMIDI");
    xml.setAttribute ("version", 1);
    xml.setAttribute ("prompt", prompt);
    xml.setAttribute ("engine", engine == Engine::Claude ? "claude" : "offline");
    xml.setAttribute ("seed", juce::String ((juce::int64) seed));
    xml.setAttribute ("seedLocked", seedLocked);
    xml.setAttribute ("key", controls.keyRoot);
    xml.setAttribute ("mode", controls.mode);
    xml.setAttribute ("tempo", controls.tempo);
    xml.setAttribute ("bars", controls.bars);
    xml.setAttribute ("muted", (int) mutedMask.load());

    xml.setAttribute ("lastPrompt", lastPrompt);
    xml.setAttribute ("lastEngine", lastEngine == Engine::Claude ? "claude" : "offline");
    xml.setAttribute ("interpretSeed", juce::String ((juce::int64) interpretSeed));

    if (hasComposition())
    {
        xml.setAttribute ("baseSpec", juce::JSON::toString (SpecJson::specToVar (baseSpec), true));
        xml.setAttribute ("spec", juce::JSON::toString (SpecJson::specToVar (spec), true));
        xml.setAttribute ("composition", juce::JSON::toString (SpecJson::compositionToVar (composition), true));
    }

    if (auto params = parameters.copyState().createXml())
        xml.addChildElement (params.release());

    copyXmlToBinary (xml, destData);
}

void PromptMidiProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::shared_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    if (xml == nullptr || ! xml->hasTagName ("PromptMIDI"))
        return;

    auto token = std::weak_ptr<bool> (aliveToken);

    auto apply = [this, xml]
    {
        prompt = xml->getStringAttribute ("prompt");
        engine = xml->getStringAttribute ("engine") == "claude" ? Engine::Claude : Engine::Offline;
        seed = (uint64_t) xml->getStringAttribute ("seed", "1").getLargeIntValue();
        seedLocked = xml->getBoolAttribute ("seedLocked");
        controls.keyRoot = xml->getIntAttribute ("key", -1);
        controls.mode = xml->getIntAttribute ("mode", -1);
        controls.tempo = xml->getIntAttribute ("tempo", 0);
        controls.bars = xml->getIntAttribute ("bars", 0);

        if (auto* params = xml->getChildByName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*params));

        pm::StyleSpec restoredSpec;
        pm::Composition restored;
        juce::String error;

        const bool haveSpec = SpecJson::specFromVar (juce::JSON::parse (xml->getStringAttribute ("spec")), restoredSpec, error);
        const bool haveComp = SpecJson::compositionFromVar (juce::JSON::parse (xml->getStringAttribute ("composition")), restored);

        lastPrompt = xml->getStringAttribute ("lastPrompt", prompt);
        lastEngine = xml->getStringAttribute ("lastEngine") == "claude" ? Engine::Claude : Engine::Offline;
        interpretSeed = (uint64_t) xml->getStringAttribute ("interpretSeed", juce::String ((juce::int64) seed)).getLargeIntValue();

        if (haveSpec)
        {
            spec = restoredSpec;
            pm::StyleSpec restoredBase;

            baseSpec = SpecJson::specFromVar (juce::JSON::parse (xml->getStringAttribute ("baseSpec")), restoredBase, error)
                         ? restoredBase : restoredSpec;
        }

        if (haveComp && restored.noteCount() > 0)
        {
            composition = restored;
            publishPlayback();
            mutedMask.store ((uint32_t) xml->getIntAttribute ("muted", 0));
            statusMessage = "Restored from the project.";
        }

        sendChangeMessage();
    };

    if (juce::MessageManager::existsAndIsCurrentThread())
        apply();
    else
        juce::MessageManager::callAsync ([token, apply] { if (auto alive = token.lock(); alive && *alive) apply(); });
}

//==============================================================================
juce::AudioProcessorEditor* PromptMidiProcessor::createEditor()
{
    return new PromptMidiEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PromptMidiProcessor();
}
