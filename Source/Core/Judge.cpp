#include "Judge.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>

namespace pm
{

const Judgement::Aspect* Judgement::find (const std::string& name) const
{
    for (auto& a : aspects)
        if (a.name == name)
            return &a;

    return nullptr;
}

namespace
{
double clamp10 (double v) { return std::max (0.0, std::min (10.0, v)); }

/** 10 inside [lo, hi], falling linearly to `floor` at `lo - span` and `hi + span`. */
double plateau (double v, double lo, double hi, double span, double floor = 3.0)
{
    if (v >= lo && v <= hi)
        return 10.0;

    const double d = v < lo ? lo - v : v - hi;
    return std::max (floor, 10.0 - (10.0 - floor) * std::min (1.0, d / span));
}

bool isPitched (const Track& t) { return ! t.isDrum && t.role != Role::Percussion; }

struct LineNote { int start, length, pitch, velocity; };

/** The tune: the melody part (the busiest one, if several), or the highest pitched part. One note
    per onset (the top one), in time order. */
std::vector<LineNote> leadLine (const Composition& c)
{
    const Track* lead = nullptr;

    for (auto& t : c.tracks)
        if (isPitched (t) && t.role == Role::Melody && t.splitFrom < 0
            && (lead == nullptr || t.notes.size() > lead->notes.size()))
            lead = &t;

    if (lead == nullptr || lead->notes.size() < 8)
    {
        double bestAvg = -1.0;

        for (auto& t : c.tracks)
        {
            if (! isPitched (t) || t.notes.size() < 8)
                continue;

            double sum = 0.0;
            for (auto& n : t.notes)
                sum += n.pitch;

            if (sum / (double) t.notes.size() > bestAvg)
            {
                bestAvg = sum / (double) t.notes.size();
                lead = &t;
            }
        }
    }

    std::vector<LineNote> line;

    if (lead == nullptr)
        return line;

    for (auto& n : lead->notes)
        line.push_back ({ n.start, n.length, n.pitch, n.velocity });

    std::sort (line.begin(), line.end(), [] (auto& a, auto& b) { return a.start != b.start ? a.start < b.start : a.pitch > b.pitch; });
    line.erase (std::unique (line.begin(), line.end(), [] (auto& a, auto& b) { return a.start == b.start; }), line.end());
    return line;
}

/** Roman numeral without accidentals or quality: "bVII7" -> "VII", "i7" -> "i", "IVmaj7" -> "IV". */
std::string romanCore (const std::string& r)
{
    size_t i = 0;

    while (i < r.size() && (r[i] == 'b' || r[i] == '#'))
        ++i;

    std::string core;

    while (i < r.size() && (r[i] == 'I' || r[i] == 'V' || r[i] == 'i' || r[i] == 'v'))
        core += r[i++];

    return core;
}

std::string upper (std::string s)
{
    for (auto& ch : s)
        ch = (char) std::toupper ((unsigned char) ch);

    return s;
}

//==============================================================================
Judgement::Aspect judgeMelody (const Composition& c, const StyleSpec& spec)
{
    Judgement::Aspect a { "Melody", 7.0, 0.0, "" };
    const auto line = leadLine (c);

    if (line.size() < 8)
    {
        a.note = "no tune to judge";
        return a;
    }

    const int beat = kPpq * 4 / std::max (1, c.tsDen);
    const int bar = std::max (1, c.ticksPerBar);

    // Range and the climax: the highest note should come once or twice, late but not at the end.
    int lo = 127, hi = 0;
    for (auto& n : line) { lo = std::min (lo, n.pitch); hi = std::max (hi, n.pitch); }

    // Range within a phrase (any 8 bars): a theme passed between instruments may span more overall.
    int phraseRange = 0;

    for (size_t i = 0, k = 0; i < line.size(); ++i)
    {
        while (line[i].start - line[k].start > 8 * bar)
            ++k;

        for (size_t m = k; m < i; ++m)
            phraseRange = std::max (phraseRange, std::abs (line[i].pitch - line[m].pitch));
    }

    const double range = plateau (phraseRange, 9, 19, 8);
    const int first = line.front().start, span = std::max (1, line.back().start - first);
    int peakAt = 0, peakCount = 0;

    for (size_t i = 0; i < line.size(); ++i)
        if (line[i].pitch == hi)
        {
            if (peakCount++ == 0)
                peakAt = (int) i;
        }

    const double peakPos = (double) (line[(size_t) peakAt].start - first) / span;
    double climax = plateau (peakPos, 0.45, 0.85, 0.4, 4.0);
    climax -= std::min (3.0, 0.5 * std::max (0, peakCount - 3));

    // Steps and leaps, within phrases; a leap should turn back by step.
    int intervals = 0, steps = 0, leaps = 0, recovered = 0;

    for (size_t i = 1; i < line.size(); ++i)
    {
        if (line[i].start - line[i - 1].start > bar)
            continue;

        const int iv = line[i].pitch - line[i - 1].pitch;
        ++intervals;
        steps += std::abs (iv) <= 2 ? 1 : 0;

        if (std::abs (iv) >= 5)
        {
            ++leaps;

            if (i + 1 < line.size() && line[i + 1].start - line[i].start <= bar)
            {
                const int next = line[i + 1].pitch - line[i].pitch;
                recovered += (next != 0 && (next > 0) != (iv > 0) && std::abs (next) <= 4) ? 1 : 0;
            }
        }
    }

    const double stepPct = intervals > 0 ? (double) steps / intervals : 0.6;
    const double motion = plateau (stepPct, 0.5, 0.82, 0.3, 4.0);
    const double recovery = leaps == 0 ? 9.0 : 5.0 + 5.0 * recovered / leaps;

    // A motif that comes back: 3-move figures (interval + rhythm) that recur. Some recurrence is
    // a theme; none is rambling; all of it is a loop.
    std::map<std::vector<int>, int> figures;
    int total = 0;

    for (size_t i = 0; i + 3 < line.size(); ++i)
    {
        if (line[i + 3].start - line[i].start > 2 * bar)
            continue;

        std::vector<int> key;

        for (size_t k = i; k < i + 3; ++k)
        {
            key.push_back (line[k + 1].pitch - line[k].pitch);
            key.push_back ((line[k + 1].start - line[k].start + kPpq / 8) / (kPpq / 4));
        }

        ++figures[key];
        ++total;
    }

    int repeated = 0;
    for (auto& f : figures)
        repeated += f.second >= 2 ? f.second : 0;

    const double recurrence = total > 0 ? (double) repeated / total : 0.5;
    const double motif = plateau (recurrence, 0.3, 0.75, 0.3, 4.0);

    // Rhythm: a tune of all-equal notes is a scale exercise.
    std::map<int, int> durations;
    for (size_t i = 1; i < line.size(); ++i)
        if (line[i].start - line[i - 1].start <= bar)
            ++durations[(line[i].start - line[i - 1].start + kPpq / 8) / (kPpq / 4)];

    int kinds = 0;
    for (auto& d : durations)
        kinds += d.second >= 2 ? 1 : 0;

    const double rhythm = kinds >= 3 ? 10.0 : kinds == 2 ? 7.5 : 4.5;

    // The last note: home, and held.
    double ending = 8.0;

    if (spec.resolveEnding)
    {
        const auto& last = line.back();
        const bool tonic = ((last.pitch - c.endKeyRoot) % 12 + 12) % 12 == 0;
        ending = tonic ? (last.length >= beat ? 10.0 : 8.0) : (last.length >= beat ? 6.0 : 4.5);
    }

    a.score = clamp10 (0.18 * range + 0.2 * climax + 0.16 * motion + 0.12 * recovery + 0.16 * motif + 0.08 * rhythm + 0.1 * ending);

    const std::pair<double, const char*> parts[] = {
        { climax, peakPos < 0.45 ? "peaks too early" : peakPos > 0.85 ? "peaks only at the very end" : "peak note comes back too often" },
        { motif, recurrence < 0.3 ? "no motif comes back" : "repeats itself too much" },
        { range, phraseRange < 9 ? "narrow range" : "very wide range" },
        { motion, stepPct < 0.5 ? "jumpy line" : "almost all steps" },
        { recovery, "leaps don't turn back" },
        { ending, "doesn't end at home" },
        { rhythm, "one rhythm throughout" } };

    const auto* weakest = &parts[0];
    for (auto& p : parts)
        if (p.first < weakest->first)
            weakest = &p;

    if (weakest->first < 7.5)
        a.note = weakest->second;
    else
        a.note = "climax at " + std::to_string ((int) std::lround (peakPos * 100)) + "%, motif returns, "
                 + std::to_string (phraseRange) + "-semitone range";

    return a;
}

//==============================================================================
Judgement::Aspect judgeHarmony (const Composition& c, const StyleSpec& spec)
{
    Judgement::Aspect a { "Harmony", 7.0, 0.0, "" };

    if (c.chords.size() < 2)
    {
        a.note = "no chords to judge";
        return a;
    }

    std::map<std::string, int> names;
    for (auto& ch : c.chords)
        ++names[ch.name];

    const int distinct = (int) names.size();
    const double variety = c.chords.size() < 6 ? 8.0 : distinct >= 4 ? 10.0 : distinct == 3 ? 8.0 : distinct == 2 ? 5.0 : 3.0;

    // The cadence: tonic at the end, approached from the dominant (or plagally).
    double cadence = 8.0;
    std::string cadenceText = "loops back";

    if (spec.resolveEnding)
    {
        const auto last = upper (romanCore (c.chords.back().roman));
        const auto before = upper (romanCore (c.chords[c.chords.size() - 2].roman));
        const bool tonic = last == "I";
        cadence = ! tonic ? 4.0 : (before == "V" || before == "VII") ? 10.0 : before == "IV" ? 9.0 : 7.0;
        cadenceText = ! tonic ? "doesn't cadence home" : (before == "V" || before == "VII") ? "perfect cadence"
                    : before == "IV" ? "plagal cadence" : "ends on the tonic";
    }

    // The bass under each chord change: a chord tone (the root, ideally).
    int changes = 0, supported = 0, rooted = 0;

    for (auto& ch : c.chords)
    {
        int lowest = 128;

        for (auto& t : c.tracks)
        {
            if (! isPitched (t))
                continue;

            for (auto& n : t.notes)
                if (n.start <= ch.start && n.start + n.length > ch.start)
                    lowest = std::min (lowest, n.pitch);
        }

        if (lowest > 127 || ch.pcs.empty())
            continue;

        ++changes;
        const int pc = lowest % 12;
        supported += std::find (ch.pcs.begin(), ch.pcs.end(), pc) != ch.pcs.end() || pc == ch.bassPc ? 1 : 0;
        rooted += pc == ch.pcs.front() || pc == ch.bassPc ? 1 : 0;
    }

    const double bass = changes == 0 ? 7.0 : 4.0 + 4.0 * supported / changes + 2.0 * rooted / changes;

    a.score = clamp10 (0.3 * variety + 0.4 * cadence + 0.3 * bass);

    if (cadence < 7.0)
        a.note = cadenceText;
    else if (variety < 7.0)
        a.note = "only " + std::to_string (distinct) + " chords";
    else if (bass < 7.0)
        a.note = "bass often off the chord";
    else
        a.note = cadenceText + ", " + std::to_string (distinct) + " chords";

    return a;
}

//==============================================================================
Judgement::Aspect judgeTexture (const Composition& c)
{
    Judgement::Aspect a { "Balance", 8.0, 0.0, "" };
    const auto line = leadLine (c);

    double harmonyPitch = 0.0, harmonyVel = 0.0;
    int harmonyNotes = 0, pitchedTracks = 0, lowest = 127, highest = 0;

    for (auto& t : c.tracks)
    {
        if (! isPitched (t) || t.notes.empty())
            continue;

        ++pitchedTracks;

        for (auto& n : t.notes)
        {
            lowest = std::min (lowest, n.pitch);
            highest = std::max (highest, n.pitch);

            if (t.role == Role::Chords || t.role == Role::Pad || t.role == Role::Arpeggio || t.role == Role::Ostinato)
            {
                harmonyPitch += n.pitch;
                harmonyVel += n.velocity;
                ++harmonyNotes;
            }
        }
    }

    if (line.size() < 8 || pitchedTracks < 2)
    {
        a.note = pitchedTracks < 2 ? "a single part" : "no tune to balance";
        return a;
    }

    double melPitch = 0.0, melVel = 0.0;
    for (auto& n : line) { melPitch += n.pitch; melVel += n.velocity; }
    melPitch /= (double) line.size();
    melVel /= (double) line.size();

    double clarity = 9.0;

    if (harmonyNotes > 0)
    {
        harmonyPitch /= harmonyNotes;
        harmonyVel /= harmonyNotes;
        const double above = melPitch - harmonyPitch;      // semitones the tune sits above the accompaniment
        const double louder = melVel - harmonyVel;
        clarity = 0.6 * plateau (above, 3.0, 30.0, 10.0, 3.0) + 0.4 * plateau (louder, -4.0, 40.0, 16.0, 4.0);
    }

    const double spread = plateau (highest - lowest, 26, 70, 16, 5.0);
    a.score = clamp10 (0.65 * clarity + 0.35 * spread);
    a.note = clarity < 7.0 ? "tune buried in the accompaniment"
           : spread < 7.0 ? "parts crowd one register"
           : "tune stands out, wide spread";
    return a;
}

//==============================================================================
/** How busy and how loud, against the energy the plan asked for (soft ~ 0.2, epic ~ 0.9). */
Judgement::Aspect judgeMood (const Composition& c, const StyleSpec& spec)
{
    Judgement::Aspect a { "Mood", 8.0, 0.0, "" };

    const int beat = kPpq * 4 / std::max (1, c.tsDen);
    const double beats = (double) std::max (1, c.lengthTicks) / beat;
    std::vector<int> onsets;
    double vel = 0.0;
    int notes = 0;

    for (auto& t : c.tracks)
    {
        if (! isPitched (t))
            continue;

        for (auto& n : t.notes)
        {
            onsets.push_back (n.start / (kPpq / 8));
            vel += n.velocity;
            ++notes;
        }
    }

    if (notes == 0)
    {
        a.note = "no pitched parts";
        return a;
    }

    std::sort (onsets.begin(), onsets.end());
    onsets.erase (std::unique (onsets.begin(), onsets.end()), onsets.end());

    double energy = std::max (0.0, std::min (1.0, spec.energy));

    if (spec.request.loudness < 0) energy = std::min (energy, 0.3);
    if (spec.request.loudness > 0) energy = std::max (energy, 0.75);

    const double activity = onsets.size() / beats;            // distinct onsets per beat
    const double targetActivity = 1.0 + 3.0 * energy;
    const double meanVel = vel / notes;
    const double targetVel = 44.0 + 46.0 * energy;   // the composer's own scale (measured across styles)

    // Asked for soft, quieter is fine; asked for loud, louder is fine.
    double off = meanVel - targetVel;
    if (spec.request.loudness < 0) off = std::max (0.0, off);
    if (spec.request.loudness > 0) off = std::min (0.0, off);

    const double busy = clamp10 (10.0 - 3.5 * std::abs (std::log2 (std::max (0.05, activity) / targetActivity)));
    const double loud = clamp10 (10.0 - std::abs (off) / 5.0);

    a.score = clamp10 (0.5 * busy + 0.5 * loud);
    a.note = busy < 7.0 ? (activity > targetActivity ? "busier than the mood" : "sparser than the mood")
           : loud < 7.0 ? (meanVel > targetVel ? "louder than the mood" : "quieter than the mood")
           : "energy fits the mood";
    return a;
}

//==============================================================================
/** The dynamic shape over the piece, against the plan's dynamics (arc, build, fade, flat). */
Judgement::Aspect judgeShape (const Composition& c, const StyleSpec& spec)
{
    Judgement::Aspect a { "Shape", 7.0, 0.0, "" };
    constexpr int segments = 8;
    double e[segments] = {};

    if (c.lengthTicks <= 0)
        return a;

    for (auto& t : c.tracks)
        for (auto& n : t.notes)
        {
            const int s = std::min (segments - 1, std::max (0, (int) ((long long) n.start * segments / c.lengthTicks)));
            e[s] += n.velocity * (t.isDrum ? 0.5 : 1.0);
        }

    const double mean = std::accumulate (e, e + segments, 0.0) / segments;

    if (mean <= 0.0)
        return a;

    double var = 0.0;
    for (double v : e) var += (v - mean) * (v - mean);
    const double cv = std::sqrt (var / segments) / mean;

    const int peak = (int) (std::max_element (e, e + segments) - e);

    // Rank correlation with time, for builds and fades.
    double slope = 0.0;
    for (int i = 0; i < segments; ++i)
        slope += (i - (segments - 1) / 2.0) * (e[i] - mean);
    slope /= mean * 21.0;   // ~ +-1 for a steady rise / fall

    double fit = 7.0;
    std::string text;

    switch (spec.dynamics)
    {
        case Dynamics::Arc:
            fit = (peak >= 3 && peak <= 6 ? 10.0 : peak == 2 || peak == 7 ? 7.0 : 5.0);
            text = peak >= 3 && peak <= 6 ? "builds to a peak and settles" : "peak in the wrong place";
            break;
        case Dynamics::Build:
            fit = clamp10 (5.0 + 5.0 * slope);
            text = slope > 0.3 ? "builds steadily" : "doesn't build";
            break;
        case Dynamics::Fade:
            fit = clamp10 (5.0 - 5.0 * slope);
            text = slope < -0.3 ? "fades away" : "doesn't fade";
            break;
        case Dynamics::Flat:
            fit = clamp10 (10.0 - 10.0 * std::max (0.0, cv - 0.35));
            text = "even throughout";
            break;
    }

    // Some contrast between sections, unless it's meant to stay level.
    const double contrast = spec.dynamics == Dynamics::Flat ? 9.0 : plateau (cv, 0.12, 0.6, 0.12, 4.0);
    a.score = clamp10 (0.65 * fit + 0.35 * contrast);
    a.note = contrast < 7.0 ? (cv < 0.12 ? "the same all the way through" : "lurches between sections") : text;
    return a;
}
} // namespace

//==============================================================================
Judgement judgeDraft (const Composition& c, const StyleSpec& spec, double takeOverlap)
{
    Judgement j;

    Judgement::Aspect request { "Your request", 10.0, 0.0, "" };
    if (c.request.total() > 0)
    {
        request.score = 10.0 * c.request.met() / c.request.total();
        request.note = std::to_string (c.request.met()) + " of " + std::to_string (c.request.total()) + " met";

        for (auto& cr : c.request.criteria)
            if (! cr.met)
            {
                request.note = "misses \"" + cr.label + "\"";
                break;
            }
    }
    else
    {
        request.note = "nothing specific asked";
    }

    Judgement::Aspect craft { "Craft", clamp10 (c.quality.score), 0.0, "" };
    craft.note = c.quality.score >= 9.5 ? "clean voicing, no clashes"
               : c.quality.outOfRange > 0 ? "notes out of range"
               : c.quality.wrongChordNotes > 0 ? "notes against the chord"
               : c.quality.lowMudPct > 20.0 ? "muddy low register"
               : c.quality.strongClashPct > 3.0 ? "harsh clashes" : "minor roughness";

    j.aspects = { request, craft, judgeMelody (c, spec), judgeHarmony (c, spec), judgeTexture (c), judgeMood (c, spec), judgeShape (c, spec) };

    const double weights[] = { 22.0, 20.0, 22.0, 12.0, 10.0, 8.0, 6.0 };

    for (size_t i = 0; i < j.aspects.size(); ++i)
        j.aspects[i].weight = weights[i];

    if (takeOverlap >= 0.0)
    {
        Judgement::Aspect fresh { "New music", clamp10 (10.0 * (1.0 - std::min (1.0, takeOverlap / 0.5))), 6.0, "" };
        fresh.note = std::to_string ((int) std::lround (takeOverlap * 100)) + "% of its tune is in an earlier take";
        j.aspects.push_back (fresh);
    }

    double sum = 0.0, weight = 0.0;

    for (auto& a : j.aspects)
    {
        sum += a.score * a.weight;
        weight += a.weight;
    }

    j.total = weight > 0.0 ? 10.0 * sum / weight : 0.0;
    return j;
}

} // namespace pm
