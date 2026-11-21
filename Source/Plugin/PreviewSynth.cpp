#include "PreviewSynth.h"

#include <cmath>

namespace
{
    constexpr double twoPi = 6.283185307179586;

    inline float polyBlep (double t, double dt)
    {
        if (t < dt)
        {
            t /= dt;
            return (float) (t + t - t * t - 1.0);
        }

        if (t > 1.0 - dt)
        {
            t = (t - 1.0) / dt;
            return (float) (t * t + t + t + 1.0);
        }

        return 0.0f;
    }

    inline float sawWave (double phase, double dt)    { return (float) (2.0 * phase - 1.0) - polyBlep (phase, dt); }

    inline float squareWave (double phase, double dt)
    {
        float v = phase < 0.5 ? 1.0f : -1.0f;
        v += polyBlep (phase, dt);
        v -= polyBlep (std::fmod (phase + 0.5, 1.0), dt);
        return v;
    }

    inline float sine (double phase) { return (float) std::sin (twoPi * phase); }

    inline float decayMultiplier (double sampleRate, double seconds)
    {
        // reaches -60 dB after `seconds`
        return (float) std::pow (0.001, 1.0 / std::max (1.0, seconds * sampleRate));
    }

    inline float attackStep (double sampleRate, double ms)
    {
        return (float) (1.0 / std::max (1.0, ms * 0.001 * sampleRate));
    }

    inline float lowpassCoeff (double sampleRate, double cutoffHz)
    {
        return (float) (1.0 - std::exp (-twoPi * cutoffHz / sampleRate));
    }
}

PreviewSynth::PreviewSynth()
{
    programs.fill (0);
    expression.fill (1.0f);

    for (int ch = 0; ch < 16; ++ch)
    {
        const double pan = ch == 9 ? 0.0 : (double) (((ch * 5) % 9) - 4) / 10.0; // -0.4 .. 0.4
        const double angle = (pan + 1.0) * juce::MathConstants<double>::pi * 0.25;
        panLeft[(size_t) ch] = (float) std::cos (angle);
        panRight[(size_t) ch] = (float) std::sin (angle);
    }
}

void PreviewSynth::prepare (double sr)
{
    sampleRate = sr > 0.0 ? sr : 44100.0;
    reset();
}

void PreviewSynth::reset()
{
    for (auto& v : voices)
        v.active = false;

    expression.fill (1.0f);
}

void PreviewSynth::setProgram (int channel, int program)
{
    if (channel >= 0 && channel < 16)
        programs[(size_t) channel] = juce::jlimit (0, 127, program);
}

PreviewSynth::Timbre PreviewSynth::timbreFor (int p)
{
    if (p == 4 || p == 5 || p == 11)                 return Timbre::Keys;
    if (p <= 15)                                     return Timbre::Pluck;
    if (p <= 23)                                     return Timbre::Organ;
    if (p <= 31)                                     return Timbre::Pluck;
    if (p <= 39)                                     return Timbre::Bass;
    if (p == 45 || p == 46)                          return Timbre::Pluck;
    if (p == 47)                                     return Timbre::Timpani;
    if (p <= 51)                                     return Timbre::Strings;
    if (p <= 55)                                     return Timbre::Pad;
    if (p <= 63)                                     return Timbre::Brass;
    if (p <= 71)                                     return Timbre::Reed;
    if (p <= 79)                                     return Timbre::Pipe;
    if (p <= 87)                                     return Timbre::Lead;
    if (p <= 103)                                    return Timbre::Pad;
    if (p <= 108 || p == 112 || p == 114)            return Timbre::Pluck;
    if (p <= 111)                                    return Timbre::Reed;
    return Timbre::Pluck;
}

void PreviewSynth::handle (const juce::MidiMessage& m)
{
    const int ch = juce::jlimit (0, 15, m.getChannel() - 1);

    if (m.isNoteOn())
    {
        noteOn (ch, m.getNoteNumber(), m.getFloatVelocity());
    }
    else if (m.isNoteOff())
    {
        noteOff (ch, m.getNoteNumber());
    }
    else if (m.isProgramChange())
    {
        programs[(size_t) ch] = m.getProgramChangeNumber();
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        for (auto& v : voices)
            if (v.active && v.channel == ch)
                v.releasing = true;
    }
    else if (m.isController())
    {
        const int cc = m.getControllerNumber();

        if (cc == 11 || cc == 1)
            expression[(size_t) ch] = 0.3f + 0.7f * (float) m.getControllerValue() / 127.0f;
    }
}

void PreviewSynth::noteOn (int ch, int note, float velocity)
{
    // Re-striking the same note retriggers its voice.
    Voice* target = nullptr;

    for (auto& v : voices)
        if (v.active && v.channel == ch && v.note == note)
            target = &v;

    if (target == nullptr)
        for (auto& v : voices)
            if (! v.active)
            {
                target = &v;
                break;
            }

    if (target == nullptr)
    {
        // Steal the oldest voice, preferring released ones.
        uint32_t oldest = UINT32_MAX;

        for (int pass = 0; pass < 2 && target == nullptr; ++pass)
            for (auto& v : voices)
                if ((pass == 1 || v.releasing) && v.age < oldest)
                {
                    oldest = v.age;
                    target = &v;
                }
    }

    Voice& v = *target;
    v = Voice();
    v.active = true;
    v.channel = ch;
    v.note = note;
    v.velocity = velocity;
    v.age = ++ageCounter;
    v.freq = 440.0 * std::pow (2.0, (note - 69) / 12.0);

    const double sr = sampleRate;

    if (ch == 9)
    {
        v.timbre = Timbre::Drum;
        v.attackStep = 1.0f;
        v.env = 1.0f;
        v.sustain = 0.0f;

        auto kind = [&] (int k, double decaySeconds) { v.drumKind = k; v.decayMul = decayMultiplier (sr, decaySeconds); };

        switch (note)
        {
            case 35: case 36:                     kind (1, 0.32); v.freq = 52.0; v.pitchEnv = 2.2f; v.pitchDecay = decayMultiplier (sr, 0.05); break;
            case 38: case 40:                     kind (2, 0.18); v.freq = 185.0; v.toneEnv = 1.0f; v.toneDecay = decayMultiplier (sr, 0.08); break;
            case 37:                              kind (9, 0.03); v.freq = 1700.0; break;
            case 39:                              kind (10, 0.16); break;
            case 42: case 44:                     kind (3, 0.045); break;
            case 46:                              kind (4, 0.32); break;
            case 49: case 52: case 55: case 57:   kind (5, 1.1); break;
            case 51: case 53: case 59:            kind (5, 0.6); break;
            case 41: case 43: case 45: case 47: case 48: case 50:
                kind (6, 0.38);
                v.freq = 70.0 + (note - 41) * 12.0;
                v.pitchEnv = 0.6f;
                v.pitchDecay = decayMultiplier (sr, 0.06);
                break;
            case 60: case 61: case 62: case 63: case 64:
                kind (7, 0.16);
                v.freq = 180.0 + (64 - note) * 45.0;
                v.pitchEnv = 0.3f;
                v.pitchDecay = decayMultiplier (sr, 0.03);
                break;
            case 54: case 69: case 70: case 82:   kind (8, 0.08); break;
            default:                              kind (7, 0.12); v.freq = 300.0; break;
        }

        return;
    }

    v.timbre = timbreFor (programs[(size_t) ch]);

    switch (v.timbre)
    {
        case Timbre::Pluck:
        {
            const double decay = juce::jlimit (0.35, 2.6, 2.6 - (note - 36) * 0.035);
            v.attackStep = attackStep (sr, 2.0);
            v.sustain = 0.0f;
            v.decayMul = decayMultiplier (sr, decay);
            v.releaseMul = decayMultiplier (sr, 0.25);
            v.filterCoeff = lowpassCoeff (sr, 5000.0);
            break;
        }
        case Timbre::Keys:
            v.attackStep = attackStep (sr, 3.0);
            v.sustain = 0.15f;
            v.decayMul = decayMultiplier (sr, 1.8);
            v.releaseMul = decayMultiplier (sr, 0.3);
            break;
        case Timbre::Organ:
            v.attackStep = attackStep (sr, 8.0);
            v.sustain = 1.0f;
            v.releaseMul = decayMultiplier (sr, 0.08);
            break;
        case Timbre::Bass:
            v.attackStep = attackStep (sr, 4.0);
            v.sustain = 0.55f;
            v.decayMul = decayMultiplier (sr, 0.5);
            v.releaseMul = decayMultiplier (sr, 0.08);
            v.filterCoeff = lowpassCoeff (sr, 900.0);
            break;
        case Timbre::Strings:
            v.attackStep = attackStep (sr, 140.0);
            v.sustain = 0.9f;
            v.decayMul = decayMultiplier (sr, 2.0);
            v.releaseMul = decayMultiplier (sr, 0.35);
            v.filterCoeff = lowpassCoeff (sr, 2200.0);
            break;
        case Timbre::Pad:
            v.attackStep = attackStep (sr, 350.0);
            v.sustain = 0.9f;
            v.decayMul = decayMultiplier (sr, 3.0);
            v.releaseMul = decayMultiplier (sr, 0.9);
            v.filterCoeff = lowpassCoeff (sr, 1400.0);
            break;
        case Timbre::Brass:
            v.attackStep = attackStep (sr, 45.0);
            v.sustain = 0.85f;
            v.decayMul = decayMultiplier (sr, 1.0);
            v.releaseMul = decayMultiplier (sr, 0.15);
            v.filterCoeff = lowpassCoeff (sr, 1800.0);
            break;
        case Timbre::Pipe:
            v.attackStep = attackStep (sr, 35.0);
            v.sustain = 0.9f;
            v.decayMul = decayMultiplier (sr, 2.0);
            v.releaseMul = decayMultiplier (sr, 0.12);
            break;
        case Timbre::Reed:
            v.attackStep = attackStep (sr, 25.0);
            v.sustain = 0.85f;
            v.decayMul = decayMultiplier (sr, 1.5);
            v.releaseMul = decayMultiplier (sr, 0.1);
            v.filterCoeff = lowpassCoeff (sr, 2200.0);
            break;
        case Timbre::Lead:
            v.attackStep = attackStep (sr, 4.0);
            v.sustain = 0.85f;
            v.decayMul = decayMultiplier (sr, 1.0);
            v.releaseMul = decayMultiplier (sr, 0.12);
            v.filterCoeff = lowpassCoeff (sr, 4000.0);
            break;
        case Timbre::Timpani:
            v.attackStep = 1.0f;
            v.attackDone = true;
            v.env = 1.0f;
            v.sustain = 0.0f;
            v.decayMul = decayMultiplier (sr, 1.1);
            v.releaseMul = decayMultiplier (sr, 0.6);
            v.pitchEnv = 0.4f;
            v.pitchDecay = decayMultiplier (sr, 0.08);
            break;
        case Timbre::Drum:
            break;
    }
}

void PreviewSynth::noteOff (int ch, int note)
{
    if (ch == 9)
        return; // drum hits ring out by themselves

    for (auto& v : voices)
        if (v.active && v.channel == ch && v.note == note)
            v.releasing = true;
}

float PreviewSynth::noise()
{
    noiseSeed ^= noiseSeed << 13;
    noiseSeed ^= noiseSeed >> 17;
    noiseSeed ^= noiseSeed << 5;
    return (float) ((double) noiseSeed / 2147483648.0 - 1.0);
}

float PreviewSynth::renderVoice (Voice& v)
{
    const double sr = sampleRate;
    float s = 0.0f;

    if (v.timbre == Timbre::Drum)
    {
        v.env *= v.decayMul;

        switch (v.drumKind)
        {
            case 1: // kick
            {
                const double f = v.freq * (1.0 + v.pitchEnv);
                v.pitchEnv *= v.pitchDecay;
                v.phase = std::fmod (v.phase + f / sr, 1.0);
                s = sine (v.phase) * 1.4f;
                break;
            }
            case 2: // snare
            {
                v.toneEnv *= v.toneDecay;
                v.phase = std::fmod (v.phase + v.freq / sr, 1.0);
                const float n = noise();
                const float hp = n - v.lastNoise;
                v.lastNoise = n;
                s = 0.55f * hp + 0.6f * sine (v.phase) * v.toneEnv;
                break;
            }
            case 3: case 4: case 5: case 8: // hats, cymbals, shakers
            {
                const float n = noise();
                const float hp = n - v.lastNoise;
                v.lastNoise = n;
                s = hp * (v.drumKind == 5 ? 0.28f : (v.drumKind == 8 ? 0.3f : 0.4f));
                break;
            }
            case 6: case 7: // toms, hand drums
            {
                const double f = v.freq * (1.0 + v.pitchEnv);
                v.pitchEnv *= v.pitchDecay;
                v.phase = std::fmod (v.phase + f / sr, 1.0);
                s = sine (v.phase) * 0.9f;
                break;
            }
            case 9: // rim / click
                v.phase = std::fmod (v.phase + v.freq / sr, 1.0);
                s = sine (v.phase) * 0.6f + noise() * 0.2f;
                break;
            case 10: // clap
            {
                const float n = noise();
                v.filterState += 0.35f * (n - v.filterState);
                s = (n - v.filterState) * 0.7f;
                break;
            }
            default:
                break;
        }

        if (v.env < 0.0005f)
            v.active = false;

        return s * v.env * v.velocity;
    }

    // Pitched voices: linear attack, exponential decay to the sustain level, exponential release.
    if (v.timbre == Timbre::Timpani)
    {
        v.env *= v.releasing ? v.releaseMul : v.decayMul;
    }
    else if (v.releasing)
    {
        v.env *= v.releaseMul;
    }
    else if (! v.attackDone)
    {
        v.env += v.attackStep;

        if (v.env >= 1.0f)
        {
            v.env = 1.0f;
            v.attackDone = true;
        }
    }
    else
    {
        v.env = v.sustain + (v.env - v.sustain) * v.decayMul;
    }

    const double dt = v.freq / sr;
    v.phase += dt;

    if (v.phase >= 1.0)
        v.phase -= 1.0;

    switch (v.timbre)
    {
        case Timbre::Pluck:
        {
            const float bright = 0.35f + 0.65f * v.env;
            s = sine (v.phase) + 0.45f * bright * sine (std::fmod (2.0 * v.phase, 1.0)) + 0.2f * bright * sine (std::fmod (3.0 * v.phase, 1.0));
            s *= 0.55f;
            break;
        }
        case Timbre::Keys:
            s = 0.7f * sine (v.phase) + 0.25f * sine (std::fmod (2.0 * v.phase, 1.0)) * v.env;
            break;
        case Timbre::Organ:
            s = 0.5f * sine (v.phase) + 0.3f * sine (std::fmod (2.0 * v.phase, 1.0)) + 0.2f * sine (std::fmod (4.0 * v.phase, 1.0));
            break;
        case Timbre::Bass:
        {
            const float raw = 0.75f * sine (v.phase) + 0.35f * sawWave (v.phase, dt);
            v.filterState += v.filterCoeff * (raw - v.filterState);
            s = v.filterState * 1.1f;
            break;
        }
        case Timbre::Strings:
        case Timbre::Brass:
        case Timbre::Reed:
        case Timbre::Lead:
        {
            const bool square = v.timbre == Timbre::Reed || (v.timbre == Timbre::Lead && programs[(size_t) v.channel] == 80);
            const float raw = square ? squareWave (v.phase, dt) : sawWave (v.phase, dt);
            v.filterState += v.filterCoeff * (raw - v.filterState);
            s = v.filterState * (v.timbre == Timbre::Lead ? 0.35f : 0.45f);
            break;
        }
        case Timbre::Pad:
        {
            const double dt2 = dt * 1.004;
            v.phase2 += dt2;

            if (v.phase2 >= 1.0)
                v.phase2 -= 1.0;

            const float raw = 0.5f * (sawWave (v.phase, dt) + sawWave (v.phase2, dt2));
            v.filterState += v.filterCoeff * (raw - v.filterState);
            s = v.filterState * 0.5f;
            break;
        }
        case Timbre::Pipe:
            s = 0.75f * sine (v.phase) + 0.12f * sine (std::fmod (2.0 * v.phase, 1.0)) + 0.03f * noise();
            break;
        case Timbre::Timpani:
        {
            const double f = v.freq * (1.0 + v.pitchEnv);
            v.pitchEnv *= v.pitchDecay;
            v.phase2 = std::fmod (v.phase2 + f / sr, 1.0);
            s = sine (v.phase2) * 0.9f + 0.15f * noise() * v.env;
            break;
        }
        case Timbre::Drum:
            break;
    }

    s *= v.env * (0.25f + 0.75f * v.velocity);

    const bool finished = v.timbre == Timbre::Timpani || v.releasing || v.sustain <= 0.0f;

    if (finished && v.attackDone && v.env < 0.0005f)
        v.active = false;

    return s;
}

void PreviewSynth::render (juce::AudioBuffer<float>& buffer, int start, int num, float gain)
{
    if (num <= 0)
        return;

    auto* left = buffer.getWritePointer (0, start);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, start) : nullptr;

    for (int i = 0; i < num; ++i)
    {
        float l = 0.0f, r = 0.0f;

        for (auto& v : voices)
        {
            if (! v.active)
                continue;

            const float s = renderVoice (v) * expression[(size_t) v.channel];
            l += s * panLeft[(size_t) v.channel];
            r += s * panRight[(size_t) v.channel];
        }

        l = std::tanh (l * gain * 0.32f);
        r = std::tanh (r * gain * 0.32f);

        left[i] += l;

        if (right != nullptr)
            right[i] += r;
    }
}
