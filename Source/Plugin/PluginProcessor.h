#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ClaudeClient.h"
#include "Composition.h"
#include "PreviewSynth.h"
#include "PromptInterpreter.h"
#include "Settings.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

/** The PromptMIDI processor.

    Message thread: turns prompts into compositions (offline, or via Claude on a worker
    thread), owns the result and its persistence, and writes .mid files.
    Audio thread: plays the composition as MIDI output - synced to the host transport,
    or from its own clock for previewing - and optionally through a small preview synth.
*/
class PromptMidiProcessor : public juce::AudioProcessor,
                            public juce::ChangeBroadcaster,
                            private juce::Timer
{
public:
    PromptMidiProcessor();
    ~PromptMidiProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }

    const juce::String getName() const override              { return JucePlugin_Name; }
    bool acceptsMidi() const override                        { return true; }
    bool producesMidi() const override                       { return true; }
    bool isMidiEffect() const override                       { return false; }
    double getTailLengthSeconds() const override             { return 0.0; }

    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    enum class Engine { Offline, Claude };
    enum class Status { Idle, Working, Error };

    /** Overrides from the editor's Key / Scale / Tempo / Bars / Length controls (-1 / 0 / "" = auto). */
    struct Controls
    {
        int keyRoot = -1;
        int mode = -1;
        int tempo = 0;
        int bars = 0;
        juce::String length;   // sketch | cue | full | extended
        std::vector<pm::InstrumentChoice> instruments; // empty = the prompt / style decides
    };

    void generate (const juce::String& prompt);
    void makeVariation();

    /** Call after changing `controls`: re-applies them to the current piece without a new request. */
    void controlsChanged();
    void cancelGeneration();
    bool isGenerating() const                                { return claudeJob != nullptr; }

    Engine engine = Engine::Offline;
    Controls controls;
    bool seedLocked = false;
    uint64_t seed = 1;
    juce::String prompt;

    const pm::StyleSpec& getSpec() const                     { return spec; }
    const pm::Composition& getComposition() const            { return composition; }
    int getCompositionVersion() const                        { return compositionVersion; }
    int getPieceVersion() const                              { return pieceVersion; }   // changes only for a new piece, not for edits
    bool hasComposition() const                              { return composition.noteCount() > 0; }

    Status getStatus() const                                 { return status; }
    juce::String getStatusMessage() const                    { return statusMessage; }

    void setTrackMuted (int index, bool muted);
    bool isTrackMuted (int index) const;
    uint64_t getMutedMask() const                            { return mutedMask.load(); }
    static constexpr int maxMutableTracks = 64;

    juce::String suggestedFileName (int trackIndex = -1) const;
    bool writeMidiFile (const juce::File&, int trackIndex = -1) const;
    juce::File writeMidiToTempFile (int trackIndex = -1) const;

    /** The in-plugin preview (heard only inside PromptMIDI, never with the host). */
    void setPreviewPlaying (bool shouldPlay, double fromTick = 0.0);

    /** Where Play starts (set by clicking the timeline ruler). */
    double getCursorTick() const                             { return cursorTick; }
    void setCursorTick (double tick);

    /** Replaces one track's notes (from the note editor), as an undoable step. */
    void editTrackNotes (int trackIndex, std::vector<pm::Note> notes, const juce::String& actionName);
    juce::UndoManager& getUndoManager()                      { return undoManager; }
    /** True when the notes differ from what was composed: edits still in the undo history, or
        made before it (a take you went back to, a restored project). Undoing every edit clears it. */
    bool isEdited() const                                    { return edited || undoManager.canUndo(); }

    /** Every Generate and Variation is a new take; earlier takes (with their edits) stay one click
        away, so nothing is lost by trying another. Kept for this session only: the project saves
        the take on screen. */
    int getNumTakes() const                                  { return (int) takes.size(); }
    int getCurrentTake() const                               { return currentTake; }
    void selectTake (int index);
    juce::String getTakeTitle (int index) const;
    static constexpr int maxTakes = 20;

    /** Plays one note through the preview sound (clicking a note in the editor). Never during host playback. */
    void auditionNote (int trackIndex, int pitch, int velocity);
    bool isPreviewPlaying() const                            { return previewPlaying.load(); }

    /** For the editor's hints: how long since the host or audio device last asked for audio
        (a preview can't play without it), and whether that was while bypassed. */
    juce::uint32 getMsSinceLastAudio() const                 { return juce::Time::getMillisecondCounter() - lastBlockMs.load(); }
    bool isBypassedByHost() const                            { return bypassedByHost.load(); }
    double getPlayheadTick() const                           { return playheadTick.load(); }

    juce::AudioProcessorValueTreeState parameters;
    SharedSettings settings;

private:
    struct PlaybackEvent
    {
        int tick;
        uint8_t order;   // note-offs before controllers before note-ons at the same tick
        uint8_t size;
        int8_t track;
        uint8_t bytes[3];
    };

    struct PlaybackData
    {
        std::vector<PlaybackEvent> events;
        std::array<int, 16> programs {};
        int lengthTicks = 0;
        int tempo = 120;
        int loopStart = 0;    // the region that repeats when looping (intro plays once, the end stinger is skipped)
        int loopEnd = 0;
    };

    /** Composes `newSpec` into the take on screen (call startNewTake() first for a new one). */
    void applySpec (const pm::StyleSpec& newSpec, uint64_t newSeed);
    void startNewTake (const juce::String& kind);   // keeps the take on screen and opens a new one; call before changing the state

    struct Take
    {
        pm::StyleSpec spec, baseSpec;
        pm::Composition composition;
        juce::String prompt;
        Engine engine = Engine::Offline;
        uint64_t seed = 1, interpretSeed = 1, variationSeed = 0, muted = 0;
        bool edited = false;
        juce::String kind;        // "Generated", "Variation", "New settings"
    };

    void storeTake();             // the state on screen -> takes[currentTake]
    void resetTakes();            // one take: the state on screen
    std::vector<Take> takes;
    int currentTake = -1;
    void setTrackNotes (int trackIndex, const std::vector<pm::Note>& notes);
    friend struct NoteEditAction;
    void publishPlayback();
    void applyControls (pm::StyleSpec&, bool includeMode) const;
    juce::String controlsDescription() const;
    pm::PromptOverrides promptOverrides() const;
    void setStatus (Status, const juce::String&);
    void timerCallback() override;
    static uint64_t randomSeed();

    void emitRange (const PlaybackData&, double fromTick, double toTick, bool inclusiveEnd,
                    double ticksPerSample, int firstSample, int numSamples);
    void allNotesOff (int samplePosition);

    pm::StyleSpec spec;       // what was composed (base spec + controls)
    pm::StyleSpec baseSpec;   // Claude's plan as received (offline plans are re-derived from the prompt)
    pm::Composition composition;
    int compositionVersion = 0;
    int pieceVersion = 0;
    juce::String lastPrompt;
    Engine lastEngine = Engine::Offline;
    uint64_t interpretSeed = 1;
    uint64_t variationSeed = 0;   // 0 = the plan as interpreted; otherwise the last Variation
    Status status = Status::Idle;
    juce::String statusMessage;
    std::unique_ptr<Claude::Job> claudeJob;
    int requestCounter = 0;

    // Message thread -> audio thread hand-over. Old data is kept alive in `retired`
    // until the audio thread has let go of it, so nothing is freed on the audio thread.
    juce::SpinLock playbackLock;
    std::shared_ptr<const PlaybackData> pendingPlayback;
    std::shared_ptr<const PlaybackData> activePlayback;
    std::vector<std::shared_ptr<const PlaybackData>> retired;

    std::atomic<uint64_t> mutedMask { 0 };
    std::atomic<bool> previewPlaying { false };
    std::atomic<bool> previewRestart { false };
    std::atomic<double> previewStartTick { 0.0 };
    std::atomic<int> auditionRequest { -1 };   // channel << 16 | program << 8 | pitch, -1 = none
    std::atomic<int> auditionVelocity { 90 };
    int auditionSamplesLeft = 0, auditionChannel = -1, auditionPitch = -1;
    double cursorTick = 0.0;
    juce::UndoManager undoManager;
    bool edited = false;          // edits from before the current undo history
    std::atomic<double> playheadTick { -1.0 };
    std::atomic<juce::uint32> lastBlockMs { 0 };
    std::atomic<bool> bypassedByHost { false };

    // Audio thread only
    double currentSampleRate = 44100.0;
    double internalTick = 0.0;
    double expectedTick = -1.0;
    bool wasPlaying = false;
    std::array<std::array<uint8_t, 128>, 16> activeNotes {};
    juce::MidiBuffer generatedMidi, panicMidi;
    PreviewSynth synth;
    bool synthHadSound = false;
    bool stopPreviewSound = false;
    int silentBlocks = 0;
    enum class Destination { None, Preview, MidiOut };
    Destination lastDestination = Destination::None;

    std::atomic<float>* previewSoundParam = nullptr;
    std::atomic<float>* previewVolumeParam = nullptr;
    std::atomic<float>* hostMidiOutParam = nullptr;
    std::atomic<float>* loopParam = nullptr;

    std::shared_ptr<bool> aliveToken = std::make_shared<bool> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PromptMidiProcessor)
};
