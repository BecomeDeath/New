#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Composer.h"
#include "MidiFileWriter.h"
#include "QualityCheck.h"
#include "PresetIO.h"
#include "RequestCheck.h"
#include "Originality.h"
#include "SpecJson.h"

namespace
{
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        return {
            // The preview sound is heard only while previewing inside PromptMIDI (its Play button).
            std::make_unique<AudioParameterBool>  (ParameterID { "previewSound", 1 }, "Preview Sound", true),
            std::make_unique<AudioParameterFloat> (ParameterID { "previewVolume", 1 }, "Preview Volume", NormalisableRange<float> (0.0f, 1.0f), 0.7f),
            // Opt-in: send the piece as MIDI out while the host plays (to drive other instruments). Never audio.
            std::make_unique<AudioParameterBool>  (ParameterID { "hostMidiOut", 2 }, "Send MIDI with Host", false),
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
    hostMidiOutParam   = parameters.getRawParameterValue ("hostMidiOut");
    loopParam          = parameters.getRawParameterValue ("loop");

    generatedMidi.ensureSize (8192);
    panicMidi.ensureSize (4096);
    statusMessage = "Describe the music you want, then press Generate.";
    synth.setSource (settings->getPreviewSound());   // the instruments load in the background
    startTimer (250);
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
                panicMidi.addEvent (juce::MidiMessage::noteOff (ch + 1, note), samplePosition);
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

    const uint64_t muted = mutedMask.load (std::memory_order_relaxed);

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
    lastBlockMs.store (juce::Time::getMillisecondCounter());
    bypassedByHost.store (false);
    synth.beginBlock();
    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    generatedMidi.clear();
    panicMidi.clear();

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
    const bool followHost = hostMidiOutParam->load() > 0.5f && hostPlaying;

    // The in-plugin preview never plays along with the host: when the host starts, it stops.
    if (hostPlaying && previewPlaying.load())
    {
        previewPlaying.store (false);
        stopPreviewSound = true;
    }

    if (stopPreviewSound)
    {
        allNotesOff (0);
        synth.reset();
        stopPreviewSound = false;
        wasPlaying = false;
        expectedTick = -1.0;
    }

    Destination destination = Destination::None;

    if (data != nullptr && data->lengthTicks > 0)
    {
        if (followHost)
        {
            playing = true;
            destination = Destination::MidiOut;
            ticksPerSample = hostBpm / 60.0 * pm::kPpq / currentSampleRate;
            startTick = hostPpq * pm::kPpq;
        }
        else if (previewPlaying.load() && ! hostPlaying)
        {
            if (previewRestart.exchange (false))
            {
                internalTick = previewStartTick.load();
                expectedTick = -1.0;
                allNotesOff (0);
            }

            playing = true;
            destination = Destination::Preview;
            ticksPerSample = data->tempo / 60.0 * pm::kPpq / currentSampleRate;
            startTick = internalTick;
            internalTick += ticksPerSample * numSamples;
        }
    }

    if (destination != lastDestination)
    {
        // Switching between preview and host output: end whatever the old destination was playing.
        allNotesOff (0);
        lastDestination = destination;
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
            // Like a game engine: play into the loop region once, then repeat it.
            const double l0 = data->loopStart, l1 = data->loopEnd > data->loopStart ? data->loopEnd : length;
            const double loopLength = juce::jmax (1.0, l1 - l0);
            double a = startTick;

            if (a < 0.0)
                a = std::fmod (a, l1) + l1;

            if (a >= l1)
                a = l0 + std::fmod (a - l0, loopLength);

            const double b = a + span;

            if (b <= l1)
            {
                emitRange (*data, a, b, false, ticksPerSample, 0, numSamples);
            }
            else
            {
                emitRange (*data, a, l1, true, ticksPerSample, 0, numSamples);
                const int wrapSample = (int) ((l1 - a) / ticksPerSample);
                allNotesOff (juce::jlimit (0, numSamples - 1, wrapSample));
                emitRange (*data, l0, l0 + (b - l1), false, ticksPerSample, wrapSample, numSamples);
            }

            playheadTick.store (b > l1 ? l0 + (b - l1) : b);
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

    // A note clicked in the editor: heard through the preview sound only, never with the host.
    if (const int req = auditionRequest.exchange (-1); req >= 0 && ! hostPlaying && previewSoundParam->load() > 0.5f)
    {
        if (auditionChannel >= 0)
            synth.handle (juce::MidiMessage::noteOff (auditionChannel + 1, auditionPitch));

        auditionChannel = (req >> 16) & 0x0F;
        auditionPitch = req & 0x7F;
        synth.setProgram (auditionChannel, (req >> 8) & 0x7F);
        synth.handle (juce::MidiMessage::noteOn (auditionChannel + 1, auditionPitch, (juce::uint8) juce::jlimit (1, 127, auditionVelocity.load())));
        auditionSamplesLeft = (int) (currentSampleRate * 0.35);
        synthHadSound = true;
        silentBlocks = 0;
    }

    if (auditionChannel >= 0)
    {
        auditionSamplesLeft -= numSamples;

        if (auditionSamplesLeft <= 0)
        {
            synth.handle (juce::MidiMessage::noteOff (auditionChannel + 1, auditionPitch));

            // Restore the composition's program on that channel.
            if (data != nullptr)
                synth.setProgram (auditionChannel, data->programs[(size_t) auditionChannel]);

            auditionChannel = -1;
        }
    }

    // Note-offs from a stop or a jump go to both destinations (harmless where nothing sounds).
    if (! panicMidi.isEmpty())
    {
        for (const auto meta : panicMidi)
            synth.handle (meta.getMessage());

        midi.addEvents (panicMidi, 0, numSamples, 0);
    }

    // The preview synth sounds only for the in-plugin preview; its audio never follows the host.
    if (destination == Destination::Preview && previewSoundParam->load() > 0.5f)
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
        // Let released notes ring out briefly after a stop, then go silent.
        synth.render (buffer, 0, numSamples, previewVolumeParam->load());

        if (++silentBlocks > 64)
        {
            synth.reset();
            synthHadSound = false;
            silentBlocks = 0;
        }
    }

    if (synthHadSound && destination == Destination::Preview)
        silentBlocks = 0;

    // Incoming MIDI passes through; with "Send MIDI with host" the piece is added on top.
    if (destination == Destination::MidiOut && ! generatedMidi.isEmpty())
        midi.addEvents (generatedMidi, 0, numSamples, 0);
}

void PromptMidiProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Bypassed: silence, end every note this plug-in started, and stop the preview.
    lastBlockMs.store (juce::Time::getMillisecondCounter());
    bypassedByHost.store (true);
    buffer.clear();
    panicMidi.clear();
    previewPlaying.store (false);
    allNotesOff (0);

    if (! panicMidi.isEmpty())
        midi.addEvents (panicMidi, 0, juce::jmax (1, buffer.getNumSamples()), 0);

    synth.reset();
    synthHadSound = false;
    wasPlaying = false;
    expectedTick = -1.0;
    lastDestination = Destination::None;
    playheadTick.store (-1.0);
}

void PromptMidiProcessor::releaseResources()
{
    synth.reset();
    previewPlaying.store (false);

    for (auto& ch : activeNotes)
        ch.fill (0);
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
    o.length = controls.length.toStdString();
    o.instruments = controls.instruments;
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

        if (s.orchestral)
            s.length = "bars";
    }

    if (s.orchestral && controls.length.isNotEmpty())
        s.length = controls.length.toStdString();

    // Claude was told the instruments; this makes sure the plan uses exactly those.
    if (! controls.instruments.empty())
        pm::applyInstrumentChoices (s, controls.instruments);

    pm::normalizeSpec (s);
}

juce::String PromptMidiProcessor::controlsDescription() const
{
    juce::StringArray parts;

    if (controls.keyRoot >= 0)  parts.add ("key " + SpecJson::keyName (controls.keyRoot));
    if (controls.mode >= 0)     parts.add ("mode " + juce::String (pm::modeInfo ((pm::Mode) controls.mode).id));
    if (controls.tempo > 0)     parts.add ("tempo " + juce::String (controls.tempo) + " BPM");
    if (controls.bars > 0)      parts.add (juce::String (controls.bars) + " bars");
    if (controls.length.isNotEmpty()) parts.add ("length " + controls.length);

    if (! controls.instruments.empty())
    {
        juce::StringArray list;

        for (auto& c : controls.instruments)
            list.add (juce::String (c.name) + (c.program >= 0 ? " (GM " + juce::String (c.program) + ")" : juce::String (" (percussion)"))
                      + (c.role != "auto" ? ", role " + juce::String (c.role) : juce::String()));

        parts.add ("use exactly these instruments, one track each, in this order: " + list.joinIntoString ("; "));
    }

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
        startNewTake ("Generated");
        lastPrompt = prompt;
        lastEngine = Engine::Offline;
        variationSeed = 0;
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

            startNewTake ("Claude");
            baseSpec = result.spec;
            baseSpec.interpretation.insert (baseSpec.interpretation.begin(),
                                            "Planned by " + (result.model.isNotEmpty() ? result.model : settings->getModel()).toStdString());
            lastPrompt = requestPrompt;
            lastEngine = Engine::Claude;
            variationSeed = 0;
            interpretSeed = newSeed;

            auto s = baseSpec;
            applyControls (s, true);

            // Claude's plan is held to the same request as the offline one: what the prompt and
            // the controls ask for (instruments, key, tempo, length...) is enforced, then checked.
            s.request = pm::parseRequest (requestPrompt.toStdString(), promptOverrides());
            pm::applyRequest (s, s.request);
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

    // Same key, tempo, length and instruments; new harmony (unless Claude planned it), new
    // accompaniment figures, and - from the new seed - a new theme, form and orchestration.
    startNewTake ("Variation");
    const auto base = spec;

    // A variation is new music: if it shares much of its melody with an earlier take, try again.
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        variationSeed = randomSeed();
        applySpec (pm::varySpec (base, variationSeed, lastEngine == Engine::Claude), variationSeed);

        if (overlapWithOtherTakes() < maxTakeOverlap)
            break;
    }
    setStatus (Status::Idle, "New variation: same key, tempo and instruments, new music. The previous take is one click back.");
}

void PromptMidiProcessor::controlsChanged()
{
    if (claudeJob != nullptr || spec.tracks.empty())
        return;

    if (isImportedPiece())
    {
        setStatus (Status::Idle, "An imported MIDI file keeps its notes. Press Variation for new music for its instruments, "
                                 "or Generate for your prompt.");
        return;
    }

    // Re-composing replaces the take on screen - unless it has edits, which are kept as their own take.
    const bool keepEdits = isEdited();
    const juce::String kept = keepEdits ? juce::String (" Your edited take is kept: go back with the take arrows.") : juce::String();

    if (keepEdits)
        startNewTake ("New settings");

    if (lastEngine == Engine::Offline)
    {
        // Same prompt, same seed, new controls: only what the controls touch changes.
        auto s = pm::interpretPrompt (lastPrompt.toStdString(), interpretSeed, promptOverrides());

        if (variationSeed != 0)
            s = pm::varySpec (s, variationSeed);

        applySpec (s, seed);
        setStatus (Status::Idle, "Updated." + kept);
        return;
    }

    // Claude's harmony was written for its scale, so a new scale needs a new plan.
    auto s = baseSpec;
    applyControls (s, false);
    auto request = pm::parseRequest (lastPrompt.toStdString(), promptOverrides());
    request.mode = -1;   // (the plan keeps Claude's scale until Generate asks for a new plan; the check still reports it)
    pm::applyRequest (s, request);
    applySpec (s, seed);

    const bool modeDiffers = controls.mode >= 0 && (pm::Mode) controls.mode != baseSpec.mode;
    setStatus (Status::Idle, (modeDiffers ? "Updated. Press Generate to have Claude re-plan in the new scale."
                                          : "Updated.") + kept);
}

void PromptMidiProcessor::startNewTake (const juce::String& kind)
{
    if (hasComposition() && currentTake >= 0)
        storeTake();   // the take we leave, with its edits

    takes.emplace_back();
    takes.back().kind = kind;

    if ((int) takes.size() > maxTakes)
        takes.erase (takes.begin());

    currentTake = (int) takes.size() - 1;
}

void PromptMidiProcessor::applySpec (const pm::StyleSpec& newSpec, uint64_t newSeed)
{
    if (takes.empty())
        startNewTake ("Generated");

    spec = newSpec;
    seed = newSeed;

    // Every take is checked against what the prompt and the controls ask for.
    if (lastPrompt.isNotEmpty())
        spec.request = pm::parseRequest (lastPrompt.toStdString(), promptOverrides());

    composition = pm::compose (spec, seed);

    if (takes.size() > 1)
    {
        const double overlap = overlapWithOtherTakes();
        composition.request.criteria.push_back ({ "Different from your other takes", overlap < maxTakeOverlap,
                                                  juce::String (juce::roundToInt (overlap * 100.0)).toStdString() + "% of its melody repeats an earlier take" });
    }

    ++pieceVersion;
    undoManager.clearUndoHistory();
    edited = false;
    cursorTick = juce::jlimit (0.0, (double) juce::jmax (0, composition.lengthTicks - 1), cursorTick);
    storeTake();
    publishPlayback();
}

double PromptMidiProcessor::overlapWithOtherTakes() const
{
    double most = 0.0;

    for (int i = 0; i < (int) takes.size(); ++i)
        if (i != currentTake && takes[(size_t) i].composition.noteCount() > 0)
            most = juce::jmax (most, pm::melodicOverlap (composition, takes[(size_t) i].composition));

    return most;
}

void PromptMidiProcessor::storeTake()
{
    if (currentTake < 0 || currentTake >= (int) takes.size())
        return;

    auto& t = takes[(size_t) currentTake];
    t.spec = spec;
    t.baseSpec = baseSpec;
    t.composition = composition;
    t.prompt = lastPrompt;
    t.engine = lastEngine;
    t.seed = seed;
    t.interpretSeed = interpretSeed;
    t.variationSeed = variationSeed;
    t.muted = mutedMask.load();
    t.edited = isEdited();
}

void PromptMidiProcessor::resetTakes()
{
    takes.clear();
    currentTake = -1;

    if (hasComposition())
    {
        takes.emplace_back();
        takes.back().kind = "Restored";
        currentTake = 0;
        storeTake();
    }
}

void PromptMidiProcessor::selectTake (int index)
{
    if (claudeJob != nullptr || index < 0 || index >= (int) takes.size() || index == currentTake)
        return;

    storeTake();
    currentTake = index;
    const auto& t = takes[(size_t) index];
    spec = t.spec;
    baseSpec = t.baseSpec;
    composition = t.composition;
    lastPrompt = t.prompt;
    lastEngine = t.engine;
    seed = t.seed;
    interpretSeed = t.interpretSeed;
    variationSeed = t.variationSeed;
    mutedMask.store (t.muted);
    edited = t.edited;   // edits made before this visit; new ones count through the undo history
    ++pieceVersion;
    undoManager.clearUndoHistory();
    cursorTick = juce::jlimit (0.0, (double) juce::jmax (0, composition.lengthTicks - 1), cursorTick);

    if (previewPlaying.load())
        setPreviewPlaying (true, cursorTick);

    setStatus (Status::Idle, "Take " + juce::String (index + 1) + " of " + juce::String ((int) takes.size())
                                 + (isEdited() ? juce::String (" (edited)") : juce::String()) + ".");
    publishPlayback();
}

juce::String PromptMidiProcessor::getTakeTitle (int index) const
{
    if (index < 0 || index >= (int) takes.size())
        return {};

    // "Variation  -  Epic Battle  -  Eb Minor, 101 BPM (edited)": takes of one prompt share a title.
    const auto& t = index == currentTake ? composition : takes[(size_t) index].composition;
    const bool takeEdited = index == currentTake ? isEdited() : takes[(size_t) index].edited;
    const auto details = juce::StringArray::fromTokens (fromStd (t.summary()), juce::String::fromUTF8 ("\xc2\xb7"), {});
    const auto dash = juce::String::fromUTF8 ("  \xe2\x80\x94  ");
    juce::String text = takes[(size_t) index].kind;
    text << (text.isEmpty() ? juce::String() : dash) << fromStd (t.title);

    if (details.size() >= 2)
        text << dash << details[0].trim() << ", " << details[1].trim();

    return text + (takeEdited ? juce::String (" (edited)") : juce::String());
}

//==============================================================================
struct NoteEditAction : public juce::UndoableAction
{
    NoteEditAction (PromptMidiProcessor& p, int t, std::vector<pm::Note> b, std::vector<pm::Note> a)
        : processor (p), track (t), before (std::move (b)), after (std::move (a)) {}

    bool perform() override  { processor.setTrackNotes (track, after); return true; }
    bool undo() override     { processor.setTrackNotes (track, before); return true; }
    int getSizeInUnits() override { return (int) (before.size() + after.size()) + 1; }

    PromptMidiProcessor& processor;
    int track;
    std::vector<pm::Note> before, after;
};

void PromptMidiProcessor::setTrackNotes (int trackIndex, const std::vector<pm::Note>& notes)
{
    if (trackIndex < 0 || trackIndex >= (int) composition.tracks.size())
        return;

    auto& t = composition.tracks[(size_t) trackIndex];
    t.notes = notes;
    std::stable_sort (t.notes.begin(), t.notes.end(), [] (const pm::Note& a, const pm::Note& b) { return a.start < b.start; });

    // Keep the piece long enough for notes drawn past the end.
    for (auto& n : t.notes)
        composition.lengthTicks = juce::jmax (composition.lengthTicks, n.start + n.length);

    composition.bars = (composition.lengthTicks + composition.ticksPerBar - 1) / juce::jmax (1, composition.ticksPerBar);
    composition.quality = pm::checkQuality (composition);
    publishPlayback();
}

void PromptMidiProcessor::editTrackNotes (int trackIndex, std::vector<pm::Note> notes, const juce::String& actionName)
{
    if (trackIndex < 0 || trackIndex >= (int) composition.tracks.size())
        return;

    // Notes stay valid: in range, at least a tick long, inside the MIDI pitch range.
    for (auto& n : notes)
    {
        n.start = juce::jmax (0, n.start);
        n.length = juce::jmax (10, n.length);
        n.pitch = juce::jlimit (0, 127, n.pitch);
        n.velocity = juce::jlimit (1, 127, n.velocity);
    }

    undoManager.beginNewTransaction (actionName);
    undoManager.perform (new NoteEditAction (*this, trackIndex, composition.tracks[(size_t) trackIndex].notes, std::move (notes)));
}

void PromptMidiProcessor::auditionNote (int trackIndex, int pitch, int velocity)
{
    if (trackIndex < 0 || trackIndex >= (int) composition.tracks.size())
        return;

    const auto& t = composition.tracks[(size_t) trackIndex];
    auditionVelocity.store (velocity);
    auditionRequest.store ((t.channel & 0x0F) << 16 | (t.isDrum ? 0 : (t.program & 0x7F)) << 8 | (pitch & 0x7F));
}

void PromptMidiProcessor::setCursorTick (double tick)
{
    cursorTick = juce::jlimit (0.0, (double) juce::jmax (0, composition.lengthTicks), tick);

    if (previewPlaying.load())
        setPreviewPlaying (true, cursorTick);

    sendChangeMessage();
}

void PromptMidiProcessor::publishPlayback()
{
    auto data = std::make_shared<PlaybackData>();
    data->lengthTicks = composition.lengthTicks;
    data->tempo = composition.tempo;
    data->loopStart = composition.loopStart >= 0 && composition.loopEnd > composition.loopStart ? composition.loopStart : 0;
    data->loopEnd = composition.loopStart >= 0 && composition.loopEnd > composition.loopStart ? composition.loopEnd : composition.lengthTicks;
    data->programs.fill (0);

    for (int ti = 0; ti < (int) composition.tracks.size() && ti < maxMutableTracks; ++ti)
    {
        const auto& t = composition.tracks[(size_t) ti];
        const uint8_t ch = (uint8_t) (t.channel & 0x0F);

        if (! t.isDrum)
            data->programs[ch] = t.program;
        else if (ch == 9)
        {
            // Orchestral percussion plays on the SoundFont's orchestra kit (bank 128, preset 48).
            const auto name = juce::String (t.name).toLowerCase();

            for (auto* word : { "bass drum", "cymbal", "snare drum", "taiko", "tam-tam", "triangle", "timpani", "gran cassa" })
                if (name.contains (word))
                    data->programs[9] = 48;
        }

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

void PromptMidiProcessor::previewSoundChanged()
{
    synth.setSource (settings->getPreviewSound());
    sendChangeMessage();
}

void PromptMidiProcessor::timerCallback()
{
    synth.refresh();   // a newly loaded SoundFont, or one chosen in another instance's settings

    // Free playback data the audio thread no longer references.
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [] (const std::shared_ptr<const PlaybackData>& p) { return p.use_count() == 1; }),
                   retired.end());
}

//==============================================================================
void PromptMidiProcessor::setTrackMuted (int index, bool muted)
{
    if (index < 0 || index >= maxMutableTracks)
        return;

    const uint64_t bit = (uint64_t) 1 << index;
    auto mask = mutedMask.load();
    mutedMask.store (muted ? (mask | bit) : (mask & ~bit));
    sendChangeMessage();
}

bool PromptMidiProcessor::isTrackMuted (int index) const
{
    return index >= 0 && index < maxMutableTracks && ((mutedMask.load() >> index) & 1u);
}

void PromptMidiProcessor::setPreviewPlaying (bool shouldPlay, double fromTick)
{
    if (shouldPlay)
    {
        previewStartTick.store (juce::jmax (0.0, fromTick));
        previewRestart.store (true);
    }

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
        std::vector<int> newIndex (toWrite.tracks.size(), -1);

        for (int i = 0; i < (int) toWrite.tracks.size(); ++i)
        {
            if (! isTrackMuted (i))
            {
                newIndex[(size_t) i] = (int) kept.size();
                kept.push_back (toWrite.tracks[(size_t) i]);
            }
        }

        for (auto& t : kept)
            if (t.splitFrom >= 0)
                t.splitFrom = t.splitFrom < (int) newIndex.size() ? newIndex[(size_t) t.splitFrom] : -1;

        if (! kept.empty())
            toWrite.tracks = kept;
    }

    pm::MidiExportOptions options;
    options.articulations = settings->getArticulationMap();
    const auto bytes = pm::writeMidiFile (toWrite, trackIndex, options);
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
namespace
{
    juce::var instrumentsToVar (const std::vector<pm::InstrumentChoice>& list)
    {
        juce::Array<juce::var> out;

        for (auto& c : list)
        {
            juce::DynamicObject::Ptr o = new juce::DynamicObject();
            o->setProperty ("name", juce::String (c.name));
            o->setProperty ("program", c.program);
            o->setProperty ("role", juce::String (c.role));
            out.add (juce::var (o.get()));
        }

        return out;
    }

    std::vector<pm::InstrumentChoice> instrumentsFromVar (const juce::var& v)
    {
        std::vector<pm::InstrumentChoice> out;

        if (auto* list = v.getArray())
        {
            for (auto& item : *list)
            {
                pm::InstrumentChoice c;
                c.name = item["name"].toString().toStdString();
                c.program = juce::jlimit (-1, 127, (int) item["program"]);
                c.role = item["role"].toString().isEmpty() ? std::string ("auto") : item["role"].toString().toStdString();

                if (! c.name.empty() && out.size() < 24)
                    out.push_back (c);
            }
        }

        return out;
    }

    /** A plan for composing new music like an imported piece: its tempo, meter, key and instruments. */
    pm::StyleSpec specLike (const pm::Composition& c)
    {
        pm::StyleSpec s;
        s.title = c.title;
        s.styleId = "imported";
        s.tempo = c.tempo;
        s.tsNum = c.tsNum;
        s.tsDen = c.tsDen;
        s.bars = juce::jlimit (4, 128, c.bars);
        s.keyRoot = c.keyRoot;
        s.mode = c.mode;
        s.progression = c.mode == pm::Mode::Minor ? std::vector<std::string> { "i", "VI", "III", "VII" }
                                                  : std::vector<std::string> { "I", "V", "vi", "IV" };
        s.length = "bars";

        for (auto& t : c.tracks)
        {
            if (t.notes.empty() || t.splitFrom >= 0)
                continue;

            pm::TrackSpec ts;
            ts.name = t.name;
            ts.role = t.role;
            ts.program = t.program;
            ts.channel = t.channel;
            int lo = 127, hi = 0, vel = 0;

            for (auto& n : t.notes)
            {
                lo = juce::jmin (lo, n.pitch);
                hi = juce::jmax (hi, n.pitch);
                vel += n.velocity;
            }

            ts.lowNote = t.isDrum ? 35 : lo;
            ts.highNote = t.isDrum ? 81 : juce::jmax (lo + 12, hi);
            ts.velocity = juce::jlimit (30, 120, vel / (int) t.notes.size());
            ts.density = juce::jlimit (0.15, 0.85, (double) t.notes.size() / juce::jmax (1, c.bars) / 8.0);
            s.tracks.push_back (ts);
        }

        pm::normalizeSpec (s);
        return s;
    }
}

bool PromptMidiProcessor::isImportedPiece() const
{
    return currentTake >= 0 && currentTake < (int) takes.size() && takes[(size_t) currentTake].kind == "Imported";
}

juce::var PromptMidiProcessor::presetToVar (const juce::String& name) const
{
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty ("format", "PromptMIDI preset");
    o->setProperty ("version", 1);
    o->setProperty ("name", name);
    o->setProperty ("prompt", prompt);
    o->setProperty ("engine", engine == Engine::Claude ? "claude" : "offline");
    o->setProperty ("seed", juce::String ((juce::int64) seed));
    o->setProperty ("seedLocked", seedLocked);

    juce::DynamicObject::Ptr c = new juce::DynamicObject();
    c->setProperty ("key", controls.keyRoot);
    c->setProperty ("mode", controls.mode);
    c->setProperty ("tempo", controls.tempo);
    c->setProperty ("bars", controls.bars);
    c->setProperty ("length", controls.length);
    c->setProperty ("instruments", instrumentsToVar (controls.instruments));
    o->setProperty ("controls", juce::var (c.get()));

    if (hasComposition())
    {
        // The piece itself, with its edits, so loading the preset brings it back exactly.
        juce::DynamicObject::Ptr p = new juce::DynamicObject();
        p->setProperty ("lastPrompt", lastPrompt);
        p->setProperty ("lastEngine", lastEngine == Engine::Claude ? "claude" : "offline");
        p->setProperty ("interpretSeed", juce::String ((juce::int64) interpretSeed));
        p->setProperty ("variationSeed", juce::String ((juce::int64) variationSeed));
        p->setProperty ("muted", juce::String ((juce::int64) mutedMask.load()));
        p->setProperty ("edited", isEdited());
        p->setProperty ("imported", isImportedPiece());
        p->setProperty ("spec", SpecJson::specToVar (spec));
        p->setProperty ("baseSpec", SpecJson::specToVar (baseSpec));
        p->setProperty ("composition", SpecJson::compositionToVar (composition));
        o->setProperty ("piece", juce::var (p.get()));
    }

    return juce::var (o.get());
}

bool PromptMidiProcessor::presetFromVar (const juce::var& v, juce::String& error)
{
    if (! v.isObject() || v["format"].toString() != "PromptMIDI preset")
    {
        error = "This isn't a PromptMIDI preset.";
        return false;
    }

    if (claudeJob != nullptr)
    {
        error = "Wait for the current generation to finish (or cancel it) first.";
        return false;
    }

    const auto& c = v["controls"];
    pm::StyleSpec restoredSpec, restoredBase;
    pm::Composition restored;
    juce::String specError;
    const auto& piece = v["piece"];
    const bool havePiece = piece.isObject()
                        && SpecJson::compositionFromVar (piece["composition"], restored) && restored.noteCount() > 0
                        && SpecJson::specFromVar (piece["spec"], restoredSpec, specError);

    prompt = v["prompt"].toString();
    engine = v["engine"].toString() == "claude" ? Engine::Claude : Engine::Offline;
    seedLocked = (bool) v["seedLocked"];
    seed = (uint64_t) juce::jmax ((juce::int64) 1, v["seed"].toString().getLargeIntValue());
    controls.keyRoot = juce::jlimit (-1, 11, c.getProperty ("key", -1).operator int());
    controls.mode = juce::jlimit (-1, (int) pm::Mode::Japanese, c.getProperty ("mode", -1).operator int());
    controls.tempo = juce::jlimit (0, 300, c.getProperty ("tempo", 0).operator int());
    controls.bars = juce::jlimit (0, 160, c.getProperty ("bars", 0).operator int());
    controls.length = c.getProperty ("length", juce::String()).toString();
    controls.instruments = instrumentsFromVar (c["instruments"]);

    if (! havePiece)
    {
        // A prompt and settings: composed fresh.
        if (prompt.trim().isEmpty())
        {
            error = "The preset has no prompt and no piece.";
            sendChangeMessage();
            return false;
        }

        generate (prompt);
        return true;
    }

    startNewTake (piece["imported"] ? "Imported" : "Preset");
    spec = restoredSpec;
    baseSpec = SpecJson::specFromVar (piece["baseSpec"], restoredBase, specError) ? restoredBase : restoredSpec;
    composition = restored;
    lastPrompt = piece["lastPrompt"].toString();
    lastEngine = piece["lastEngine"].toString() == "claude" ? Engine::Claude : Engine::Offline;
    interpretSeed = (uint64_t) piece["interpretSeed"].toString().getLargeIntValue();
    variationSeed = (uint64_t) piece["variationSeed"].toString().getLargeIntValue();
    mutedMask.store ((uint64_t) piece["muted"].toString().getLargeIntValue());
    undoManager.clearUndoHistory();
    edited = (bool) piece["edited"];
    ++pieceVersion;
    cursorTick = 0.0;
    storeTake();
    setStatus (Status::Idle, "Loaded the preset \"" + v["name"].toString() + "\".");
    publishPlayback();
    return true;
}

bool PromptMidiProcessor::savePreset (const juce::File& file, const juce::String& name) const
{
    file.getParentDirectory().createDirectory();
    return file.replaceWithText (juce::JSON::toString (presetToVar (name), false));
}

bool PromptMidiProcessor::loadPreset (const juce::File& file, juce::String& error)
{
    if (file.getSize() > 64 * 1024 * 1024)
    {
        error = file.getFileName() + " is too large to be a preset.";
        return false;
    }

    const auto v = juce::JSON::parse (file);

    if (! v.isObject())
    {
        error = file.getFileName() + " isn't a PromptMIDI preset.";
        return false;
    }

    return presetFromVar (v, error);
}

void PromptMidiProcessor::loadFactoryPreset (int index)
{
    const auto& list = PresetIO::factoryPresets();

    if (index < 0 || index >= (int) list.size() || claudeJob != nullptr)
        return;

    const auto& f = list[(size_t) index];
    controls = Controls();
    controls.length = f.length;
    seedLocked = false;
    engine = Engine::Offline;
    generate (f.prompt);
}

bool PromptMidiProcessor::importMidi (const juce::File& file, juce::String& error)
{
    if (claudeJob != nullptr)
    {
        error = "Wait for the current generation to finish (or cancel it) first.";
        return false;
    }

    pm::Composition imported;

    if (! PresetIO::readMidiFile (file, imported, error))
        return false;

    startNewTake ("Imported");
    spec = specLike (imported);
    baseSpec = spec;
    composition = imported;
    lastPrompt = {};
    lastEngine = Engine::Offline;
    variationSeed = 0;
    interpretSeed = seed;
    mutedMask.store (0);
    undoManager.clearUndoHistory();
    edited = false;
    ++pieceVersion;
    cursorTick = 0.0;
    storeTake();

    int playing = 0;

    for (auto& t : composition.tracks)
        playing += t.notes.empty() ? 0 : 1;

    setStatus (Status::Idle, "Imported " + file.getFileName() + ": " + juce::String (playing) + " tracks, "
                                 + juce::String (composition.bars) + " bars. Play, edit and drag it out - or press Variation "
                                 "for new music for the same instruments.");
    publishPlayback();
    return true;
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
    xml.setAttribute ("length", controls.length);

    {
        juce::Array<juce::var> list;

        for (auto& c : controls.instruments)
        {
            juce::DynamicObject::Ptr o = new juce::DynamicObject();
            o->setProperty ("name", juce::String (c.name));
            o->setProperty ("program", c.program);
            o->setProperty ("role", juce::String (c.role));
            list.add (juce::var (o.get()));
        }

        xml.setAttribute ("instruments", juce::JSON::toString (juce::var (list), true));
    }
    xml.setAttribute ("muted", juce::String ((juce::int64) mutedMask.load()));
    xml.setAttribute ("edited", isEdited());

    xml.setAttribute ("lastPrompt", lastPrompt);
    xml.setAttribute ("lastEngine", lastEngine == Engine::Claude ? "claude" : "offline");
    xml.setAttribute ("interpretSeed", juce::String ((juce::int64) interpretSeed));
    xml.setAttribute ("variationSeed", juce::String ((juce::int64) variationSeed));

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
        controls.length = xml->getStringAttribute ("length");
        controls.instruments.clear();

        if (auto* list = juce::JSON::parse (xml->getStringAttribute ("instruments")).getArray())
        {
            for (auto& v : *list)
            {
                pm::InstrumentChoice c;
                c.name = v["name"].toString().toStdString();
                c.program = juce::jlimit (-1, 127, (int) v["program"]);
                c.role = v["role"].toString().isEmpty() ? std::string ("auto") : v["role"].toString().toStdString();

                if (! c.name.empty())
                    controls.instruments.push_back (c);
            }
        }

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
        variationSeed = (uint64_t) xml->getStringAttribute ("variationSeed", "0").getLargeIntValue();

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
            ++pieceVersion;
            undoManager.clearUndoHistory();
            edited = xml->getBoolAttribute ("edited");
            mutedMask.store ((uint64_t) xml->getStringAttribute ("muted", "0").getLargeIntValue());
            resetTakes();
            publishPlayback();
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
