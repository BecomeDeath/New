#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

/** A small General-MIDI-ish sketch synth used to audition the generated MIDI.

    It is not meant to sound like a sample library: it gives each instrument family a
    recognisable character (plucked, bowed, blown, pads, drums) so you can judge the
    notes before routing them to your own instruments. Real-time safe: no allocation.
*/
class PreviewSynth
{
public:
    PreviewSynth();

    void prepare (double sampleRate);
    void reset();

    void setProgram (int channel, int program);
    void handle (const juce::MidiMessage&);
    void render (juce::AudioBuffer<float>& buffer, int startSample, int numSamples, float gain);

private:
    enum class Timbre { Pluck, Keys, Organ, Bass, Strings, Pad, Brass, Pipe, Reed, Lead, Timpani, Drum };

    struct Voice
    {
        bool active = false;
        bool releasing = false;
        bool attackDone = false;
        int channel = 0, note = 0;
        Timbre timbre = Timbre::Pluck;
        double freq = 440.0, phase = 0.0, phase2 = 0.0;
        float velocity = 0.0f;
        float env = 0.0f;
        float attackStep = 1.0f, decayMul = 1.0f, sustain = 1.0f, releaseMul = 0.99f;
        float filterState = 0.0f, filterCoeff = 0.5f;
        float hpState = 0.0f, lastNoise = 0.0f;
        float pitchEnv = 0.0f, pitchDecay = 0.999f;
        float toneEnv = 0.0f, toneDecay = 0.999f;
        int drumKind = 0;
        uint32_t age = 0;
    };

    static Timbre timbreFor (int program);
    void noteOn (int channel, int note, float velocity);
    void noteOff (int channel, int note);
    float renderVoice (Voice&);
    float noise();

    static constexpr int maxVoices = 48;
    std::array<Voice, maxVoices> voices;
    std::array<int, 16> programs {};
    std::array<float, 16> expression {};
    std::array<float, 16> panLeft {}, panRight {};
    double sampleRate = 44100.0;
    uint32_t noiseSeed = 0x12345678u;
    uint32_t ageCounter = 0;
};
