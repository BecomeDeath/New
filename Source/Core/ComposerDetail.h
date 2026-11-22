#pragma once

// Internal building blocks shared by the composer (Composer.cpp) and the
// orchestral cue engine (CueEngine.cpp). Not part of the public API.

#include "Composition.h"
#include "Rng.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <string>
#include <vector>

namespace pm
{
namespace detail
{

constexpr double kPi = 3.14159265358979323846;
constexpr int kStep = kPpq / 4; // a 16th note
constexpr int kNoIndex = INT_MIN;

template <typename T>
T clampv (T v, T lo, T hi) { return std::max (lo, std::min (hi, v)); }

//==============================================================================
struct Meter
{
    int num = 4, den = 4;
    int ticksPerBar = 4 * kPpq;
    int beatsPerBar = 4;
    int beatTicks = kPpq;      // the felt beat (a dotted quarter in 6/8)
    int subdivTicks = kPpq / 2;
    int stepsPerBar = 16;      // 16th-note steps per bar
    bool compound = false;
};

Meter makeMeter (int num, int den);

struct ChordEvent
{
    int start = 0, end = 0;
    Chord chord;
    std::vector<int> pcs;
    std::string roman;
    int keyRoot = 0;
    int bassPc = -1;   // pedal point / inversion: the bass plays this instead of the root
    int bassRoot() const { return bassPc >= 0 ? bassPc : chord.rootPc; }
};

/** A stretch of the form: 'I' intro (before the melody), 'A' main theme, 'B' contrast. */
struct Section
{
    int startBar = 0;
    int bars = 0;
    char type = 'A';
    int keyRoot = 0;
    double intensity = 0.5;  // cue mode: 0 = hushed, 1 = full tutti
};

struct RawNote
{
    int start = 0;
    int length = 0;
    int pitch = 60;
    double velocity = 90.0;
    int micro = 0; // strum / roll offset, applied after swing and humanisation
    Articulation articulation = Articulation::Default;
};

using Notes = std::vector<RawNote>;
using Controls = std::vector<ControlChange>;

//==============================================================================
struct Context
{
    explicit Context (const StyleSpec& s) : spec (s), meter (makeMeter (s.tsNum, s.tsDen)), mi (modeInfo (s.mode)) {}

    const StyleSpec& spec;
    Meter meter;
    const ModeInfo& mi;
    int bars = 0;
    int totalTicks = 0;
    int tonicPitch = 60;
    std::vector<ChordEvent> chords;
    std::vector<Section> sections;
    Notes leadMelody;
    bool hasBassTrack = false;
    std::vector<double> barIntensity; // cue mode: per-bar intensity (0-1); empty = use spec.dynamics

    int partStart (const TrackSpec& t) const   { return t.entryBar * meter.ticksPerBar; }
    int partEndBar (const TrackSpec& t) const  { return t.endBar > t.entryBar ? std::min (bars, t.endBar) : bars; }
    int partEnd (const TrackSpec& t) const     { return std::min (totalTicks, partEndBar (t) * meter.ticksPerBar); }

    const Section& sectionAt (int tick) const
    {
        const int bar = tick / meter.ticksPerBar;

        for (auto& sec : sections)
            if (bar >= sec.startBar && bar < sec.startBar + sec.bars)
                return sec;

        return sections.back();
    }

    /** The melody scale in the key of the section containing `tick`. */

    int chordIndexAt (int tick) const
    {
        auto it = std::upper_bound (chords.begin(), chords.end(), tick,
                                    [] (int t, const ChordEvent& c) { return t < c.start; });
        return it == chords.begin() ? 0 : (int) (it - chords.begin()) - 1;
    }

    const ChordEvent& chordAt (int tick) const { return chords[(size_t) chordIndexAt (tick)]; }

    const ChordEvent* chordAfter (const ChordEvent& c) const
    {
        const auto idx = (size_t) (&c - chords.data()) + 1;
        return idx < chords.size() ? &chords[idx] : nullptr;
    }

    double energyAt (int tick) const
    {
        if (! barIntensity.empty())
        {
            // Interpolate between bar intensities so crescendos are smooth.
            const double pos = clampv ((double) tick / meter.ticksPerBar, 0.0, (double) barIntensity.size() - 1.0);
            const auto i = (size_t) pos;
            const double frac = pos - (double) i;
            const double a = barIntensity[i];
            const double b = barIntensity[std::min (barIntensity.size() - 1, i + 1)];
            return 0.62 + 0.62 * (a + (b - a) * frac);
        }

        const double t = totalTicks > 0 ? clampv ((double) tick / totalTicks, 0.0, 1.0) : 0.0;
        double shape = 1.0;

        switch (spec.dynamics)
        {
            case Dynamics::Flat:  shape = 1.0; break;
            case Dynamics::Build: shape = 0.78 + 0.34 * t; break;
            case Dynamics::Arc:   shape = 0.84 + 0.26 * std::sin (kPi * t); break;
            case Dynamics::Fade:  shape = 1.06 - 0.36 * t; break;
        }

        return shape * (0.85 + 0.3 * spec.energy);
    }

    bool hasFinalBar() const     { return spec.resolveEnding && bars > 1; }
    int finalBarStart() const    { return (bars - 1) * meter.ticksPerBar; }
    bool inFinalBar (int tick) const { return hasFinalBar() && tick >= finalBarStart(); }

    ScaleMap melodyScale() const  { return { mi.melodyScale, tonicPitch }; }
    ScaleMap harmonyScale() const { return { mi.harmonyScale, tonicPitch }; }

    /** Chord events overlapping [from, to), clipped. */
    std::vector<ChordEvent> chordsBetween (int from, int to) const
    {
        std::vector<ChordEvent> out;

        for (auto& c : chords)
        {
            if (c.end <= from || c.start >= to)
                continue;

            auto clipped = c;
            clipped.start = std::max (c.start, from);
            clipped.end = std::min (c.end, to);
            out.push_back (clipped);
        }

        return out;
    }
};


//==============================================================================
Meter makeMeter (int num, int den);
bool isRinging (int program);
bool isPianoLike (int program);
bool isGuitarLike (int program);

// Voicing
std::vector<int> chordToneOrder (const Chord& c);
std::vector<int> wantedPcs (const Chord& c, int voices, bool dropRoot);
std::vector<int> voiceChord (const std::vector<int>& wanted, int rootPc, int low, int high,
                             const std::vector<int>& prev, int maxSpread, bool rootInBass);
int bassPitchFor (int pc, int low, int high, int prev);
int fifthAbove (int pitch, int high);

// Melody helpers
enum class MelStyle { Lyrical = 0, Rhythmic = 1, Sparse = 2 };

struct RNote
{
    int offset = 0;   // relative to bar start
    int dur = 0;
    bool rest = false;
};

std::vector<RNote> fillRhythm (const Meter& m, int from, int to, double density, MelStyle style, Rng& rng, bool allowLeadingRest);
std::vector<RNote> cadenceRhythm (const Meter& m, bool finalNote, bool pickup, double density, MelStyle style, Rng& rng);
bool isStrongPosition (const Meter& m, int offsetInBar, int dur);
bool indexIsChordTone (const ScaleMap& sm, int idx, const ChordEvent& ch);
int chooseMelodyIndex (int prev, int prevDelta, int target, int lo, int hi, const ChordEvent& ch,
                       bool strong, bool allowRepeat, Rng& rng, const ScaleMap& sm, int leapBias = 0, bool onBeat = false);

enum class CadencePref { Tonic, Root, Open };
int cadenceIndex (const ScaleMap& sm, int prev, int lo, int hi, const ChordEvent& ch, CadencePref pref, int tonicPc);
int snapToChordTone (const ScaleMap& sm, int idx, int lo, int hi, const ChordEvent& ch);
int melodyPitchAt (const Notes& melody, int tick);

// Part generators (each respects the track's entryBar / endBar)
Notes doubleMelody (const Context& ctx, const TrackSpec& t);
Notes generateCounterMelody (Context& ctx, const TrackSpec& t, Rng& rng);
void addHit (Notes& out, const std::vector<int>& pitches, int start, int length, double velocity, int rollTicks = 0, bool descending = false);
void generateChords (Context& ctx, const TrackSpec& t, Rng& rng, Notes& out, Controls& cc, bool isPad);
Notes generateArpeggio (Context& ctx, const TrackSpec& t, Rng& rng);
Notes generateBass (Context& ctx, const TrackSpec& t, Rng& rng);
Notes generateOstinato (Context& ctx, const TrackSpec& t, Rng& rng);
Notes generateDrums (Context& ctx, const TrackSpec& t, Rng& rng);
void addExpression (const Context& ctx, const TrackSpec& t, const Notes& notes, Controls& cc);
Track finalizeTrack (const Context& ctx, const TrackSpec& ts, Notes raw, Controls cc, Rng& rng);
uint64_t hashString (const std::string& s);

} // namespace detail
} // namespace pm
