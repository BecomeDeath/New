#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>

#include "PreviewSynth.h"

#include <array>
#include <atomic>
#include <memory>

struct tsf;

//==============================================================================
/** The instruments the preview plays: a General MIDI SoundFont (GeneralUser GS by default, or
    your own .sf2), loaded once per process on a background thread and shared by every PromptMIDI
    instance. Played with TinySoundFont (MIT, ThirdParty/TinySoundFont).
*/
class SoundBank : private juce::Thread
{
public:
    SoundBank();
    ~SoundBank() override;

    /** "" = the built-in instruments, "sketch" = none (the sketch synth), or the path of a .sf2.
        Loading happens in the background; nothing changes if the source is the same. */
    void setSource (const juce::String& source);
    juce::String getSource() const;

    /** The loaded bank, or nullptr (loading, "sketch", or the file couldn't be loaded). */
    std::shared_ptr<tsf> getBank() const;

    /** Changes every time the bank is replaced (loaded, failed or switched off). */
    int getGeneration() const                    { return generation.load(); }
    bool isLoading() const                       { return loading.load(); }

    /** For the UI: "GeneralUser GS", "Loading instruments...", "Sketch synth", "Couldn't load ...". */
    juce::String getDescription() const;

    /** Blocks until the current load has finished (tests, offline tools). */
    bool waitUntilLoaded (int timeoutMs);

    /** The bundled bank: inside the plug-in bundle (Contents/Resources), next to the app, in an
        installed PromptMIDI.vst3 nearby, or in the user folder below. */
    static juce::File findBuiltInSoundFont();

    /** <user application data>/PromptMIDI/Sounds - SoundFonts placed here are found too. */
    static juce::File userSoundFontFolder();

    static constexpr const char* builtInFileName = "GeneralUser-GS.sf2";

private:
    void run() override;

    mutable juce::CriticalSection lock;
    std::shared_ptr<tsf> bank;
    juce::String source { "<none>" }, description { "Sketch synth" };
    std::atomic<int> generation { 0 };
    std::atomic<bool> loading { false };
    juce::WaitableEvent finished { true };
};

using SharedSoundBank = juce::SharedResourcePointer<SoundBank>;

//==============================================================================
/** One instance's player for the shared bank. The message thread builds a player for the current
    bank (and sample rate) and hands it over; the audio thread takes it at the start of a block.
    Nothing is allocated or freed on the audio thread.
*/
class SampledSynth
{
public:
    SampledSynth();
    ~SampledSynth();

    // Message thread
    void prepare (double sampleRate);
    void setBank (std::shared_ptr<tsf> bank);   // nullptr = no sampled sound
    void collectGarbage();

    // Audio thread
    void beginBlock();
    bool isActive() const;
    void reset();
    void setProgram (int channel, int program);
    void handle (const juce::MidiMessage&);
    void render (juce::AudioBuffer<float>&, int start, int num, float gain);

private:
    struct Player;
    void build();

    std::shared_ptr<tsf> currentBank;
    double sampleRate = 44100.0;
    std::atomic<Player*> pending { nullptr }, retired { nullptr };
    Player* active = nullptr;
    std::array<int, 16> programs {};

    static constexpr int chunk = 256;
    std::array<float, chunk * 2> interleaved {};
    std::array<float, chunk> left {}, right {};
    juce::Reverb reverb;
};

//==============================================================================
/** What the processor plays the preview through: the sampled instruments when they're loaded,
    the sketch synth otherwise (while loading, when chosen in Settings, or without a SoundFont).
*/
class PreviewEngine
{
public:
    PreviewEngine();

    // Message thread
    void prepare (double sampleRate);
    void setSource (const juce::String& source);   // see SoundBank::setSource
    void refresh();                                // picks up a newly loaded bank; call regularly
    juce::String getDescription() const;
    bool isSampled() const                       { return usingSampled.load(); }
    SoundBank& getBank()                         { return *bank; }

    // Audio thread (the same calls as PreviewSynth)
    void beginBlock();
    void reset();
    void setProgram (int channel, int program);
    void handle (const juce::MidiMessage&);
    void render (juce::AudioBuffer<float>&, int start, int num, float gain);

private:
    SharedSoundBank bank;
    PreviewSynth sketch;
    SampledSynth sampled;
    int shownGeneration = -1;
    bool sampledActive = false;            // audio thread
    std::atomic<bool> usingSampled { false };
};
