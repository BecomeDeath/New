// Unit tests for the engine (no JUCE needed). Run with ctest or ./promptmidi_tests

#include "Composer.h"
#include "MidiFileWriter.h"
#include "PromptInterpreter.h"
#include "QualityCheck.h"
#include "RequestCheck.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        ++checks;                                                                         \
        if (! (cond)) {                                                                   \
            ++failures;                                                                   \
            std::printf ("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                \
        }                                                                                 \
    } while (0)

#define CHECK_EQ(a, b)                                                                    \
    do {                                                                                  \
        ++checks;                                                                         \
        const auto va = (a);                                                              \
        const auto vb = (b);                                                              \
        if (! (va == vb)) {                                                               \
            ++failures;                                                                   \
            std::printf ("  FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b);         \
        }                                                                                 \
    } while (0)

using namespace pm;

static std::string nameOf (const std::string& symbol, int key, Mode mode)
{
    Chord c;

    if (! parseChordSymbol (symbol, key, mode, c))
        return "<parse error>";

    return chordName (c, keyPrefersFlats (key, mode));
}

static void testChordParsing()
{
    std::printf ("chord parsing\n");
    CHECK_EQ (nameOf ("I", 0, Mode::Major), std::string ("C"));
    CHECK_EQ (nameOf ("vi", 0, Mode::Major), std::string ("Am"));
    CHECK_EQ (nameOf ("V7", 0, Mode::Major), std::string ("G7"));
    CHECK_EQ (nameOf ("IVmaj7", 0, Mode::Major), std::string ("Fmaj7"));
    CHECK_EQ (nameOf ("ii7", 0, Mode::Major), std::string ("Dm7"));
    CHECK_EQ (nameOf ("bVII", 0, Mode::Major), std::string ("A#"));       // C major prefers sharps
    CHECK_EQ (nameOf ("bVII", 5, Mode::Major), std::string ("Eb"));       // F major prefers flats
    CHECK_EQ (nameOf ("V7/V", 0, Mode::Major), std::string ("D7"));
    CHECK_EQ (nameOf ("iim7b5", 9, Mode::Minor), std::string ("Bm7b5"));
    CHECK_EQ (nameOf ("vii\xC2\xB0", 0, Mode::Major), std::string ("Bdim"));
    CHECK_EQ (nameOf ("viio7", 0, Mode::HarmonicMinor), std::string ("Bdim7"));
    CHECK_EQ (nameOf ("i", 9, Mode::Minor), std::string ("Am"));
    CHECK_EQ (nameOf ("VI", 9, Mode::Minor), std::string ("F"));
    CHECK_EQ (nameOf ("III", 9, Mode::Minor), std::string ("C"));
    CHECK_EQ (nameOf ("V", 9, Mode::Minor), std::string ("E"));
    CHECK_EQ (nameOf ("Imaj9", 0, Mode::Major), std::string ("Cmaj9"));
    CHECK_EQ (nameOf ("ii9", 0, Mode::Major), std::string ("Dm9"));
    CHECK_EQ (nameOf ("Isus4", 0, Mode::Major), std::string ("Csus4"));
    CHECK_EQ (nameOf ("IV7", 0, Mode::Blues), std::string ("F7"));
    CHECK_EQ (nameOf ("bII", 9, Mode::Minor), std::string ("A#"));
    // absolute chord names are accepted too
    CHECK_EQ (nameOf ("F#m7", 0, Mode::Major), std::string ("F#m7"));
    CHECK_EQ (nameOf ("Bbmaj9", 0, Mode::Major), std::string ("A#maj9"));
    CHECK_EQ (nameOf ("Am", 0, Mode::Major), std::string ("Am"));
    CHECK_EQ (nameOf ("Gsus4", 0, Mode::Major), std::string ("Gsus4"));

    Chord junk;
    CHECK (! parseChordSymbol ("hello", 0, Mode::Major, junk));
    CHECK (! parseChordSymbol ("", 0, Mode::Major, junk));

    int pc = -1;
    CHECK (pitchClassFromString ("Eb", pc) && pc == 3);
    CHECK (pitchClassFromString ("f#", pc) && pc == 6);
    CHECK (pitchClassFromString ("B", pc) && pc == 11);
    CHECK (! pitchClassFromString ("H", pc));

    Mode m;
    CHECK (modeFromString ("Harmonic Minor", m) && m == Mode::HarmonicMinor);
    CHECK (modeFromString ("aeolian", m) && m == Mode::Minor);
    CHECK_EQ (keyName (2, Mode::Minor), std::string ("D Minor"));
    CHECK_EQ (keySignatureAccidentals (2, Mode::Minor), -1); // D minor: one flat
    CHECK_EQ (keySignatureAccidentals (7, Mode::Major), 1);  // G major: one sharp
}

static bool hasTrackWithProgram (const StyleSpec& s, int program)
{
    for (auto& t : s.tracks)
        if (t.role != Role::Percussion && t.program == program)
            return true;

    return false;
}

static bool hasRole (const StyleSpec& s, Role r)
{
    for (auto& t : s.tracks)
        if (t.role == r)
            return true;

    return false;
}

static void testPromptInterpretation()
{
    std::printf ("prompt interpretation\n");

    {
        const auto s = interpretPrompt ("Generate me a fantasy orchestral midi", 1);
        CHECK_EQ (s.styleId, std::string ("fantasy_orchestral"));
        CHECK_EQ (s.title, std::string ("Fantasy Orchestral"));
        CHECK (hasTrackWithProgram (s, 46)); // harp
        CHECK (hasTrackWithProgram (s, 48)); // strings
        CHECK (hasRole (s, Role::Melody));
        CHECK (s.tracks.size() >= 6);
    }
    {
        const auto s = interpretPrompt ("Make me a soft piano midi", 1);
        CHECK_EQ (s.styleId, std::string ("solo_piano"));
        CHECK_EQ (s.title, std::string ("Soft Piano"));
        CHECK (! hasRole (s, Role::Percussion));

        for (auto& t : s.tracks)
        {
            CHECK_EQ (t.program, 0);
            CHECK (t.velocity < 75);
        }
    }
    {
        const auto s = interpretPrompt ("jazz in Bb major at 140 bpm, 24 bars", 3);
        CHECK_EQ (s.styleId, std::string ("jazz"));
        CHECK_EQ (s.keyRoot, 10);
        CHECK (s.mode == Mode::Major);
        CHECK_EQ (s.tempo, 140);
        CHECK_EQ (s.bars, 24);
    }
    {
        const auto s = interpretPrompt ("dark lofi beat in f sharp minor, no drums", 5);
        CHECK_EQ (s.styleId, std::string ("lofi"));
        CHECK_EQ (s.keyRoot, 6);
        CHECK (s.mode == Mode::Minor);
        CHECK (! hasRole (s, Role::Percussion));
    }
    {
        const auto s = interpretPrompt ("epic trailer music with choir and harp", 2);
        CHECK_EQ (s.styleId, std::string ("cinematic_epic"));
        CHECK (hasTrackWithProgram (s, 52));
        CHECK (hasTrackWithProgram (s, 46));
    }
    {
        const auto s = interpretPrompt ("solo violin", 2);
        CHECK_EQ ((int) s.tracks.size(), 1);
        CHECK_EQ (s.tracks[0].program, 40);
        CHECK (s.tracks[0].role == Role::Melody);
    }
    {
        const auto s = interpretPrompt ("just piano and cello, sad", 2);
        CHECK (hasTrackWithProgram (s, 0));
        CHECK (hasTrackWithProgram (s, 42));
        CHECK (s.tracks.size() == 3);
        CHECK (s.mode == Mode::Minor || s.mode == Mode::Dorian || s.mode == Mode::HarmonicMinor);
    }
    {
        const auto s = interpretPrompt ("a waltz", 2);
        CHECK_EQ (s.tsNum, 3);
        CHECK_EQ (s.tsDen, 4);
    }
    {
        const auto s = interpretPrompt ("celtic tavern jig", 2);
        CHECK_EQ (s.styleId, std::string ("celtic_folk"));
        CHECK_EQ (s.tsNum, 6);
        CHECK_EQ (s.tsDen, 8);
    }
    {
        const auto s = interpretPrompt ("30 second synthwave track at 120 bpm", 2);
        CHECK_EQ (s.styleId, std::string ("synthwave"));
        CHECK_EQ (s.tempo, 120);
        CHECK_EQ (s.bars, 16); // 30s at 120 bpm = 15 bars -> rounded to 16
    }
    {
        const auto s = interpretPrompt ("drum and bass", 2);
        CHECK_EQ (s.styleId, std::string ("dnb"));
    }
    {
        const auto s = interpretPrompt ("rock song with a guitar solo", 2);
        CHECK_EQ (s.styleId, std::string ("rock"));
        CHECK (hasRole (s, Role::Percussion)); // "solo" here is not "solo guitar"
    }
    {
        const auto s = interpretPrompt ("asdkjh qwe", 2);
        CHECK_EQ (s.styleId, std::string ("default_cinematic"));
        CHECK (! s.tracks.empty());
    }
    {
        // UI overrides change only what they touch: same seed, same tempo/progression/instruments.
        const auto base = interpretPrompt ("dreamy synthwave", 21);
        PromptOverrides o;
        o.keyRoot = 4;
        const auto keyed = interpretPrompt ("dreamy synthwave", 21, o);
        CHECK_EQ (keyed.keyRoot, 4);
        CHECK (keyed.mode == base.mode);
        CHECK_EQ (keyed.tempo, base.tempo);
        CHECK (keyed.progression == base.progression);
        CHECK_EQ (keyed.tracks.size(), base.tracks.size());

        o.tempo = 133;
        o.bars = 32;
        o.mode = (int) Mode::Dorian;
        const auto all = interpretPrompt ("dreamy synthwave", 21, o);
        CHECK_EQ (all.tempo, 133);
        CHECK_EQ (all.bars, 32);
        CHECK (all.mode == Mode::Dorian);
    }
    {
        // variations: different seeds give different (but valid) choices
        std::set<int> tempos;

        for (uint64_t seed = 1; seed <= 12; ++seed)
            tempos.insert (interpretPrompt ("pop song", seed).tempo);

        CHECK (tempos.size() > 3);
    }
}

static int noteOnCountInMidi (const std::vector<uint8_t>& d, int& tracksOut)
{
    // Minimal SMF reader, enough to validate the writer.
    if (d.size() < 14 || d[0] != 'M' || d[1] != 'T' || d[2] != 'h' || d[3] != 'd')
        return -1;

    tracksOut = (d[10] << 8) | d[11];
    size_t pos = 14;
    int noteOns = 0;

    for (int t = 0; t < tracksOut; ++t)
    {
        if (pos + 8 > d.size() || d[pos] != 'M' || d[pos + 1] != 'T' || d[pos + 2] != 'r' || d[pos + 3] != 'k')
            return -2;

        const uint32_t len = (uint32_t) (d[pos + 4] << 24 | d[pos + 5] << 16 | d[pos + 6] << 8 | d[pos + 7]);
        size_t p = pos + 8;
        const size_t end = p + len;
        bool sawEnd = false;

        while (p < end)
        {
            while (d[p] & 0x80) ++p; // delta
            ++p;

            const uint8_t status = d[p++];

            if (status == 0xFF)
            {
                const uint8_t type = d[p++];
                uint32_t mlen = 0;

                while (d[p] & 0x80) mlen = (mlen << 7) | (d[p++] & 0x7F);
                mlen = (mlen << 7) | d[p++];

                p += mlen;

                if (type == 0x2F)
                    sawEnd = true;
            }
            else if ((status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0)
            {
                p += 1;
            }
            else if ((status & 0xF0) >= 0x80)
            {
                if ((status & 0xF0) == 0x90 && d[p + 1] > 0)
                    ++noteOns;

                p += 2;
            }
            else
            {
                return -3; // running status is never written
            }
        }

        if (! sawEnd || p != end)
            return -4;

        pos = end;
    }

    return noteOns;
}

static void checkComposition (const Composition& c, const std::string& label)
{
    bool ok = true;

    for (auto& t : c.tracks)
    {
        std::map<int, int> lastEnd;

        for (auto& n : t.notes)
        {
            ok = ok && n.start >= 0 && n.length > 0 && n.start + n.length <= c.lengthTicks;
            ok = ok && n.pitch >= 0 && n.pitch <= 127 && n.velocity >= 1 && n.velocity <= 127;

            auto it = lastEnd.find (n.pitch);

            if (it != lastEnd.end())
                ok = ok && n.start >= it->second; // no overlapping duplicates of a pitch

            lastEnd[n.pitch] = n.start + n.length;
        }

        ok = ok && (t.isDrum ? t.channel == 9 : t.channel != 9);
    }

    if (! ok)
        std::printf ("  invalid composition for %s\n", label.c_str());

    CHECK (ok);
    CHECK (c.noteCount() > 0);
}

static void testComposer()
{
    std::printf ("composer\n");

    const auto spec = interpretPrompt ("Generate me a fantasy orchestral midi", 42);
    const auto a = compose (spec, 42);
    const auto b = compose (spec, 42);
    const auto c = compose (spec, 43);

    checkComposition (a, "fantasy");
    CHECK_EQ (a.noteCount(), b.noteCount());
    CHECK (writeMidiFile (a) == writeMidiFile (b));   // deterministic
    CHECK (writeMidiFile (a) != writeMidiFile (c));   // seeds vary the result
    CHECK_EQ (a.lengthTicks, a.bars * 4 * kPpq);
    CHECK (a.bars >= spec.bars);   // orchestral prompts compose a whole cue

    // Melody: strong beats should mostly land on chord tones, and the piece ends on the tonic.
    for (auto& t : a.tracks)
    {
        if (t.role != Role::Melody || t.notes.empty())
            continue;

        int strong = 0, chordTones = 0;

        for (auto& n : t.notes)
        {
            if (n.start % (2 * kPpq) > 30)
                continue;

            ++strong;

            for (auto& slot : a.chords)
            {
                if (n.start >= slot.start - 30 && n.start < slot.end)
                {
                    Chord ch;
                    parseChordSymbol (slot.name, 0, Mode::Major, ch);

                    if (ch.containsPc (n.pitch))
                        ++chordTones;

                    break;
                }
            }
        }

        CHECK (strong > 0 && chordTones * 10 >= strong * 7);

        if (spec.resolveEnding)
            CHECK_EQ (wrapPc (t.notes.back().pitch), a.endKeyRoot);

        break;
    }

    int tracks = 0;
    const auto bytes = writeMidiFile (a);
    CHECK_EQ (noteOnCountInMidi (bytes, tracks), a.noteCount());
    CHECK_EQ (tracks, (int) a.tracks.size() + 1);

    const auto single = writeMidiFile (a, 0);
    CHECK_EQ (noteOnCountInMidi (single, tracks), (int) a.tracks[0].notes.size());
    CHECK_EQ (tracks, 2);
}

/** Every chord-playing part must play the notes of the chord it is on (no major chords on minor harmony). */
static void testPartsFollowTheHarmony()
{
    std::printf ("parts follow the harmony\n");

    for (auto& st : builtInStyles())
    {
        for (uint64_t seed = 1; seed <= 3; ++seed)
        {
            const auto spec = interpretPrompt (st.displayName + " orchestral", seed);
            const auto comp = compose (spec, seed);

            for (auto& t : comp.tracks)
            {
                // Songs: the chord and pad parts. Cues reassign every section passage by passage,
                // so there the held and struck chord notes are checked, whoever plays them.
                const bool songPart = ! spec.orchestral && (t.role == Role::Chords || t.role == Role::Pad);

                if (t.isDrum || (! songPart && ! spec.orchestral))
                    continue;

                int wrong = 0, total = 0;

                for (auto& n : t.notes)
                {
                    const bool chordal = n.articulation == Articulation::Sustain || n.articulation == Articulation::Tremolo
                                      || n.articulation == Articulation::Marcato || n.articulation == Articulation::Pizzicato
                                      || n.articulation == Articulation::Staccato;

                    if (spec.orchestral && ! chordal)
                        continue;

                    for (auto& slot : comp.chords)
                    {
                        if (n.start + 40 >= slot.start && n.start + 40 < slot.end)
                        {
                            Chord ch;
                            parseChordSymbol (slot.name, 0, Mode::Major, ch);
                            const bool inChord = slot.pcs.empty() ? ch.containsPc (n.pitch)
                                               : std::find (slot.pcs.begin(), slot.pcs.end(), wrapPc (n.pitch)) != slot.pcs.end();
                            ++total;
                            wrong += (inChord || wrapPc (n.pitch) == slot.bassPc) ? 0 : 1;
                            break;
                        }
                    }
                }

                if (wrong > 0)
                    std::printf ("  %s / %s: %d of %d chord notes outside the chord\n", st.displayName.c_str(), t.name.c_str(), wrong, total);

                CHECK (wrong == 0);
            }
        }
    }
}

static void testEveryStyleAndMeter()
{
    std::printf ("all styles x seeds x meters\n");

    const char* extras[] = { "", " in 3/4", " in 6/8", " 12/8", " 2/4", " loop", " 4 bars", " 7 bars", " 64 bars", " very fast", " slow and dark" };

    for (auto& st : builtInStyles())
    {
        for (uint64_t seed = 1; seed <= 4; ++seed)
        {
            for (auto* extra : extras)
            {
                const std::string prompt = st.displayName + extra;
                const auto spec = interpretPrompt (prompt, seed);
                const auto comp = compose (spec, seed);
                checkComposition (comp, prompt);

                int tracks = 0;
                CHECK_EQ (noteOnCountInMidi (writeMidiFile (comp), tracks), comp.noteCount());
            }
        }
    }
}

static void testThemeAndHandWrittenSpec()
{
    std::printf ("hand-written spec with theme\n");

    StyleSpec s;
    s.title = "Theme Test";
    s.keyRoot = 2;
    s.mode = Mode::Dorian;
    s.tempo = 96;
    s.bars = 8;
    s.progression = { "i", "IV", "i", "VII" };
    s.theme = { { 0, 1.0, false }, { 2, 0.5, false }, { 4, 0.5, false }, { 3, 1.0, false }, { 1, 1.0, false },
                { 0, 2.0, false }, { 0, 1.0, true }, { -1, 1.0, false } };
    s.tracks = { { "Lead", Role::Melody, 73, -1, 62, 88, 0.5, 90, 0, "lyrical" },
                 { "Pad", Role::Pad, 89, -1, 50, 76, 0.2, 70, 0, "swell" },
                 { "Bass", Role::Bass, 33, -1, 28, 50, 0.5, 90, 0, "walking" },
                 { "Kit", Role::Percussion, 0, -1, 35, 81, 0.5, 90, 2, "funk" } };

    const auto c = compose (s, 9);
    checkComposition (c, "theme");

    // The first notes of the melody follow the theme: D, F, A, G (D dorian degrees 0, 2, 4, 3).
    const auto& lead = c.tracks[0].notes;
    CHECK (lead.size() >= 4);

    if (lead.size() >= 4)
    {
        CHECK_EQ (wrapPc (lead[0].pitch), 2);
        CHECK_EQ (wrapPc (lead[1].pitch), 5);
        CHECK_EQ (wrapPc (lead[2].pitch), 9);
        CHECK_EQ (wrapPc (lead[3].pitch), 7);
    }

    CHECK (! c.tracks[1].controls.empty()); // swell pad writes CC1/CC11
    CHECK (c.tracks[3].isDrum && c.tracks[3].channel == 9);
    CHECK (c.tracks[3].notes.front().start >= 2 * 4 * kPpq - 20); // drums enter at bar 3
}


//==============================================================================
// Orchestral cues

struct MidiStats
{
    int tracks = 0, noteOns = 0, keyswitches = 0, cc32 = 0, markers = 0, cues = 0;
    std::vector<std::string> markerTexts;
};

static MidiStats scanMidi (const std::vector<uint8_t>& d)
{
    MidiStats st;
    size_t pos = 14;

    while (pos + 8 <= d.size())
    {
        const uint32_t len = (uint32_t) d[pos + 4] << 24 | (uint32_t) d[pos + 5] << 16 | (uint32_t) d[pos + 6] << 8 | d[pos + 7];
        size_t p = pos + 8;
        const size_t end = p + len;
        ++st.tracks;

        while (p < end)
        {
            while (d[p] & 0x80) ++p;
            ++p;
            const uint8_t status = d[p++];

            if (status == 0xFF)
            {
                const uint8_t type = d[p++];
                uint32_t mlen = 0;

                while (d[p] & 0x80) mlen = (mlen << 7) | (d[p++] & 0x7F);
                mlen = (mlen << 7) | d[p++];

                if (type == 0x06) { ++st.markers; st.markerTexts.push_back (std::string ((const char*) &d[p], mlen)); }
                if (type == 0x07) ++st.cues;
                p += mlen;
            }
            else if ((status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0)
            {
                p += 1;
            }
            else
            {
                if ((status & 0xF0) == 0x90 && d[p + 1] > 0)
                {
                    ++st.noteOns;
                    st.keyswitches += d[p] < 20 ? 1 : 0;
                }

                if ((status & 0xF0) == 0xB0 && d[p] == 32)
                    ++st.cc32;

                p += 2;
            }
        }

        pos = end;
    }

    return st;
}

static const char* orchestralPrompts[] = {
    "Generate me a fantasy orchestral midi",
    "Epic orchestral battle music for a boss fight",
    "Heroic main theme for an adventure game",
    "Dark fantasy orchestral theme for a villain",
    "Sad emotional orchestral lament",
    "Mysterious orchestral exploration music for ancient ruins",
    "Tense orchestral stealth music",
    "Peaceful orchestral village theme",
    "Sci-fi space orchestral theme",
    "Whimsical magical orchestral music",
    "Fast orchestral action chase",
    "Pirate orchestral adventure on the high seas",
    "Celtic orchestral music for a highland journey",
    "Japanese orchestral theme for a samurai duel",
    "Romantic orchestral waltz for a royal ball",
};

static void testCueFormAndLength()
{
    std::printf ("cue form, length and loop points\n");

    struct L { const char* length; double minSec, maxSec; };
    const L lengths[] = { { "sketch", 15, 60 }, { "cue", 45, 100 }, { "full", 115, 190 }, { "extended", 180, 280 } };

    for (auto* prompt : { orchestralPrompts[0], orchestralPrompts[1], orchestralPrompts[7] })
    {
        for (auto& l : lengths)
        {
            PromptOverrides o;
            o.length = l.length;
            const auto spec = interpretPrompt (prompt, 7, o);
            CHECK (spec.orchestral);
            const auto c = compose (spec, 7);
            checkComposition (c, std::string (prompt) + " / " + l.length);

            const double sec = c.seconds();

            if (sec < l.minSec || sec > l.maxSec)
                std::printf ("  %s / %s: %.0f s\n", prompt, l.length, sec);

            CHECK (sec >= l.minSec && sec <= l.maxSec);
            CHECK (! c.form.empty());
            CHECK (! c.markers.empty());
            CHECK (std::is_sorted (c.markers.begin(), c.markers.end(), [] (const Marker& a, const Marker& b) { return a.tick < b.tick; }));

            if (std::string (l.length) != "sketch")
            {
                // A real cue: intro, theme, development or contrast, climax and an ending.
                CHECK (c.form.find ("Theme") != std::string::npos);
                CHECK (c.form.find ("Climax") != std::string::npos);
                CHECK (c.form.find ("Outro") != std::string::npos || c.form.find ("Stinger") != std::string::npos);
            }

            if (std::string (l.length) == "full" || std::string (l.length) == "extended")
                CHECK (c.form.find ("Development") != std::string::npos && c.form.find ("Second Theme") != std::string::npos);

            // Harmony is not one looped progression: many different chords, and cadences.
            std::set<std::string> romans;

            for (auto& ch : c.chords)
                romans.insert (ch.roman);

            CHECK (romans.size() >= (std::string (l.length) == "sketch" ? 4u : (std::string (l.length) == "cue" ? 6u : 7u)));
        }
    }

    // Loops: an end stinger after the loop region, and loop markers / cue points in the file.
    const auto loopSpec = interpretPrompt ("Epic orchestral battle loop for a boss fight", 3);
    const auto loop = compose (loopSpec, 3);
    CHECK (loop.loopStart >= 0 && loop.loopEnd > loop.loopStart && loop.loopEnd < loop.lengthTicks);
    CHECK (loop.form.find ("Stinger") != std::string::npos);
    CHECK_EQ (loop.loopStart % loop.ticksPerBar, 0);
    CHECK_EQ (loop.loopEnd % loop.ticksPerBar, 0);

    const auto stats = scanMidi (writeMidiFile (loop));
    CHECK_EQ (stats.markers, (int) loop.markers.size());
    CHECK_EQ (stats.cues, 2);
    CHECK (std::find (stats.markerTexts.begin(), stats.markerTexts.end(), "Loop Start") != stats.markerTexts.end());
}

static void testCueQuality()
{
    std::printf ("every cue passes the quality check\n");

    for (auto* prompt : orchestralPrompts)
    {
        for (uint64_t seed = 1; seed <= 3; ++seed)
        {
            const auto spec = interpretPrompt (prompt, seed);
            const auto c = compose (spec, seed);
            const auto q = checkQuality (c);

            if (q.score < 8.5 || q.wrongChordNotes > 0 || q.outOfRange > 0 || q.strongClashPct >= 6.0 || q.lowMudPct >= 5.0)
                std::printf ("  %s (seed %d): %.1f, %d wrong, %d out of range, %.1f%% clash, %.1f%% mud\n", prompt, (int) seed,
                             q.score, q.wrongChordNotes, q.outOfRange, q.strongClashPct, q.lowMudPct);

            CHECK (q.score >= 8.5);
            CHECK_EQ (q.wrongChordNotes, 0);
            CHECK_EQ (q.outOfRange, 0);
            CHECK (q.strongClashPct < 6.0);   // syncopated passing tones in the tunes are allowed
            CHECK (q.lowMudPct < 5.0);
            CHECK (q.endsOnTonic);
            CHECK (c.quality.candidates >= 1);
            CHECK (std::abs (c.quality.score - q.score) < 1e-9);
        }
    }
}

static void testFullOrchestraAndArticulations()
{
    std::printf ("full orchestra, articulations and their export\n");

    const auto spec = interpretPrompt ("Dark fantasy orchestral theme for a villain", 4);
    const auto c = compose (spec, 4);

    auto find = [&] (const char* name) -> const Track*
    {
        for (auto& t : c.tracks)
            if (t.name == name)
                return &t;

        return nullptr;
    };

    for (auto* name : { "Violins I", "Violins II", "Violas", "Celli", "Basses", "Horns", "Trumpets", "Trombones", "Tuba", "Timpani", "Choir" })
    {
        if (find (name) == nullptr)
            std::printf ("  missing %s\n", name);

        CHECK (find (name) != nullptr);
    }

    // Score order: woodwinds and brass above the strings.
    int horns = -1, violins = -1;

    for (int i = 0; i < (int) c.tracks.size(); ++i)
    {
        if (c.tracks[(size_t) i].name == "Horns")     horns = i;
        if (c.tracks[(size_t) i].name == "Violins I") violins = i;
    }

    CHECK (horns >= 0 && violins > horns);

    // Orchestral percussion as separate instruments, not one drum kit.
    int percussion = 0;

    for (auto& t : c.tracks)
        percussion += t.isDrum ? 1 : 0;

    CHECK (percussion >= 3);

    std::set<Articulation> used;
    int splitParts = 0, splitNotes = 0;

    for (auto& t : c.tracks)
    {
        for (auto& n : t.notes)
            used.insert (n.articulation);

        if (t.splitFrom >= 0)
        {
            ++splitParts;
            splitNotes += (int) t.notes.size();
            CHECK (t.splitFrom < (int) c.tracks.size() && c.tracks[(size_t) t.splitFrom].splitFrom < 0);
        }
    }

    for (auto a : { Articulation::Legato, Articulation::Sustain, Articulation::Spiccato, Articulation::Marcato })
        CHECK (used.count (a) > 0);

    // General MIDI export: every note, one track per part.
    const auto gm = scanMidi (writeMidiFile (c));
    CHECK_EQ (gm.noteOns, c.noteCount());
    CHECK_EQ (gm.tracks, (int) c.tracks.size() + 1);
    CHECK_EQ (gm.keyswitches, 0);

    // Keyswitches: split parts fold back into their sections, and switches are announced.
    MidiExportOptions ks;
    ks.articulations = ArticulationMap::keyswitchDefaults();
    const auto k = scanMidi (writeMidiFile (c, -1, ks));
    CHECK_EQ (k.tracks, (int) c.tracks.size() + 1 - splitParts);
    CHECK (k.keyswitches > 0);
    CHECK_EQ (k.noteOns - k.keyswitches, c.noteCount());

    // UACC: CC32 values instead of notes.
    MidiExportOptions cc;
    cc.articulations = ArticulationMap::uaccDefaults();
    const auto u = scanMidi (writeMidiFile (c, -1, cc));
    CHECK (u.cc32 > 0);
    CHECK_EQ (u.keyswitches, 0);
    CHECK_EQ (u.noteOns, c.noteCount());
    (void) splitNotes;
}

static void testThemeDevelopment()
{
    std::printf ("themes are developed, not just repeated\n");

    const auto spec = interpretPrompt ("Heroic main theme for an adventure game", 5);
    const auto c = compose (spec, 5);

    // Different sections give the tune to different instruments.
    std::set<std::string> leads;

    for (size_t m = 0; m + 1 < c.markers.size(); ++m)
    {
        if (c.markers[m].text.rfind ("Loop", 0) == 0)
            continue;

        const int from = c.markers[m].tick, to = c.markers[m + 1].tick;
        int best = -1, bestCount = 0;

        for (int ti = 0; ti < (int) c.tracks.size(); ++ti)
        {
            int count = 0;

            for (auto& n : c.tracks[(size_t) ti].notes)
                count += (n.articulation == Articulation::Legato && n.start >= from && n.start < to) ? 1 : 0;

            if (count > bestCount)
            {
                bestCount = count;
                best = ti;
            }
        }

        if (best >= 0)
            leads.insert (c.tracks[(size_t) best].name);
    }

    CHECK (leads.size() >= 3);

    // The restatement is reharmonised: its chords differ from the first statement's somewhere.
    std::vector<std::string> first, second;
    int themeTick = -1, againTick = -1, afterTick = -1;

    for (size_t m = 0; m < c.markers.size(); ++m)
    {
        if (c.markers[m].text == "Theme")          themeTick = c.markers[m].tick;
        if (c.markers[m].text == "Theme (tutti)")  againTick = c.markers[m].tick;
        if (againTick >= 0 && afterTick < 0 && c.markers[m].tick > againTick) afterTick = c.markers[m].tick;
    }

    CHECK (themeTick >= 0 && againTick > themeTick);

    for (auto& ch : c.chords)
    {
        if (ch.start >= themeTick && ch.start < againTick)  first.push_back (ch.roman);
        if (ch.start >= againTick && ch.start < afterTick)  second.push_back (ch.roman);
    }

    CHECK (! first.empty() && ! second.empty() && first != second);
}

//==============================================================================
// Variety, the learned models and custom instruments

#include "Learned.h"

static void testLearnedModels()
{
    std::printf ("learned models\n");
    CHECK (learned::numMelodyCounts > 1000);
    CHECK (learned::numRhythmPatterns > 100);
    CHECK (learned::numProgressions > 100);

    detail::FamilyMix mix;
    mix.weight[learned::Cinematic] = 1.0;
    Rng rng (5);

    // Progressions parse in their mode, differ from what is avoided, and vary.
    std::set<std::vector<std::string>> seen;

    for (Mode mode : { Mode::Major, Mode::Minor, Mode::HarmonicMinor })
    {
        for (int i = 0; i < 20; ++i)
        {
            const auto p = detail::learnedProgression (mix, mode, rng, { { "i", "VI", "III", "VII" } });
            CHECK (p.size() == 4);

            for (auto& r : p)
            {
                Chord c;
                CHECK (parseChordSymbol (r, 0, mode, c));
            }

            CHECK (p != std::vector<std::string> ({ "i", "VI", "III", "VII" }));
            seen.insert (p);
        }
    }

    CHECK (seen.size() >= 25);
    CHECK (detail::learnedProgression (mix, Mode::Dorian, rng).empty()); // no data: the caller falls back

    // Bar rhythms fill the bar exactly, start cleanly, and get busier with density.
    for (int num : { 4, 3, 6 })
    {
        const auto m = detail::makeMeter (num, num == 6 ? 8 : 4);
        int sparse = 0, busy = 0;

        for (int i = 0; i < 40; ++i)
        {
            for (double density : { 0.15, 0.85 })
            {
                const auto r = detail::learnedBarRhythm (m, density, detail::MelStyle::Lyrical, mix, true, rng);
                CHECK (! r.empty());
                int pos = 0, notes = 0;

                for (auto& n : r)
                {
                    CHECK (n.offset == pos && n.dur > 0);
                    pos += n.dur;
                    notes += n.rest ? 0 : 1;
                }

                CHECK_EQ (pos, m.ticksPerBar);
                (density < 0.5 ? sparse : busy) += notes;
            }
        }

        CHECK (busy > sparse);
    }

    // The interval model prefers steps to wild leaps.
    CHECK (detail::learnedStepLogProb (mix, false, 1, 1, 1) > detail::learnedStepLogProb (mix, false, 1, 1, 7));
}

static void testVariationsDiffer()
{
    std::printf ("generations and variations differ\n");

    for (auto* prompt : { "Generate me a fantasy orchestral midi", "Make me a soft piano midi", "Epic orchestral battle music" })
    {
        const auto spec = interpretPrompt (prompt, 11);
        std::set<std::vector<std::string>> harmonies;
        std::set<std::string> forms, themes;

        for (uint64_t v = 1; v <= 6; ++v)
        {
            const auto varied = varySpec (spec, v * 7919);
            CHECK (varied.keyRoot == spec.keyRoot && varied.mode == spec.mode && varied.tempo == spec.tempo);
            CHECK (varied.tracks.size() == spec.tracks.size());
            const auto c = compose (varied, 1000 + v);
            harmonies.insert (varied.progression);
            forms.insert (c.form);

            std::string tune;

            for (auto& t : c.tracks)
                if (t.role == Role::Melody)
                {
                    for (size_t n = 0; n < t.notes.size() && n < 16; ++n)
                        tune += std::to_string (t.notes[n].pitch) + ":" + std::to_string (t.notes[n].start % c.ticksPerBar) + " ";
                    break;
                }

            themes.insert (tune);
            CHECK (checkQuality (c).score >= 8.0);
        }

        if (harmonies.size() < 4 || themes.size() < 6)
            std::printf ("  %s: %d progressions, %d forms, %d themes in 6 variations\n", prompt, (int) harmonies.size(), (int) forms.size(), (int) themes.size());

        CHECK (harmonies.size() >= 4);
        CHECK (themes.size() == 6);
    }

    // Cues: the form and who leads change between generations.
    std::set<std::string> forms;

    for (uint64_t seed = 1; seed <= 8; ++seed)
        forms.insert (compose (interpretPrompt ("Heroic main theme for an adventure game", seed), seed).form);

    CHECK (forms.size() >= 3);
}

static std::vector<InstrumentChoice> choose (std::initializer_list<const char*> names)
{
    std::vector<InstrumentChoice> out;

    for (auto* n : names)
    {
        InstrumentChoice c;
        c.name = n;

        for (auto& e : instrumentCatalogue())
            if (e.name == n)
                c.program = e.program;

        out.push_back (c);
    }

    return out;
}

static void testCustomInstruments()
{
    std::printf ("custom instruments\n");
    CHECK (instrumentCatalogue().size() > 50);

    auto make = [] (const char* prompt, std::vector<InstrumentChoice> inst, const char* length = "")
    {
        PromptOverrides o;
        o.instruments = std::move (inst);
        o.length = length;
        const auto spec = interpretPrompt (prompt, 3, o);
        return std::make_pair (spec, compose (spec, 3));
    };

    // One piano: a solo piece, melody and accompaniment on one track.
    {
        const auto [spec, c] = make ("sad orchestral lament", choose ({ "Piano" }), "full");
        CHECK (! spec.orchestral);
        CHECK_EQ ((int) c.tracks.size(), 1);
        CHECK (c.tracks[0].name == "Piano" && c.tracks[0].program == 0);
        CHECK (c.seconds() > 110.0 && c.seconds() < 200.0);

        int high = 0, low = 0;

        for (auto& n : c.tracks[0].notes)
            (n.pitch >= 60 ? high : low)++;

        CHECK (high > 20 && low > 20); // both hands
        CHECK (checkQuality (c).score >= 8.5);
        checkComposition (c, "solo piano");
    }

    // One melodic instrument: just the tune.
    {
        const auto [spec, c] = make ("heroic theme", choose ({ "Flutes" }));
        CHECK_EQ ((int) c.tracks.size(), 1);
        CHECK (c.tracks[0].program == 73 && c.tracks[0].role == Role::Melody);
    }

    // A duo and a band: sensible roles, nothing else added.
    {
        const auto [spec, c] = make ("peaceful village", choose ({ "Celli", "Harp" }));
        CHECK_EQ ((int) c.tracks.size(), 2);
        CHECK (spec.tracks[0].role == Role::Melody && spec.tracks[1].role == Role::Arpeggio);
        CHECK (checkQuality (c).lowMudPct < 5.0);
    }
    {
        const auto [spec, c] = make ("rock song", choose ({ "Electric Guitar", "Bass", "Drums" }));
        CHECK_EQ ((int) c.tracks.size(), 3); // the guitar plays tune and chords on its one track

        auto roleOf = [&] (const char* name)
        {
            for (auto& t : spec.tracks)
                if (t.name == name)
                    return t.role;

            return Role::Pad;
        };

        CHECK (roleOf ("Bass") == Role::Bass && roleOf ("Drums") == Role::Percussion && roleOf ("Electric Guitar") == Role::Melody);
    }

    // A user-set role is kept.
    {
        auto list = choose ({ "Oboes", "Clarinets", "Bassoons" });
        list[1].role = "melody";
        const auto [spec, c] = make ("pastoral", list);
        CHECK (spec.tracks[1].role == Role::Melody && spec.tracks[0].role != Role::Melody);
    }

    // Three or more pitched instruments in an orchestral style: a full cue, only those instruments.
    {
        const auto [spec, c] = make ("epic orchestral battle", choose ({ "Violins I", "Violins II", "Violas", "Celli", "Timpani" }), "full");
        CHECK (spec.orchestral);
        CHECK (c.seconds() > 110.0);
        std::set<std::string> names;

        for (auto& t : c.tracks)
            names.insert (t.name.substr (0, t.name.find (" (")));

        CHECK (names == std::set<std::string> ({ "Violins I", "Violins II", "Violas", "Celli", "Timpani" }));
        CHECK (checkQuality (c).score >= 8.5);
    }

    // A plan from elsewhere (Claude) is rebuilt around the user's instruments.
    {
        auto spec = interpretPrompt ("jazz combo", 2);
        applyInstrumentChoices (spec, choose ({ "Alto Sax", "Piano", "Upright Bass", "Drums" }));
        CHECK_EQ ((int) spec.tracks.size(), 4);
        CHECK (spec.tracks[0].role == Role::Melody && spec.tracks[1].role == Role::Chords && spec.tracks[2].role == Role::Bass);
        CHECK (compose (spec, 2).noteCount() > 50);
    }
}

static void testRequests()
{
    std::printf ("requests: the piece is what was asked for\n");

    auto playing = [] (const Composition& c)
    {
        std::vector<const Track*> out;

        for (auto& t : c.tracks)
            if (! t.notes.empty())
                out.push_back (&t);

        return out;
    };

    auto onlyPrograms = [&] (const Composition& c, std::initializer_list<std::pair<int, int>> ranges, bool drumsAllowed)
    {
        for (auto* t : playing (c))
        {
            if (t->isDrum)
            {
                if (! drumsAllowed)
                    return false;

                continue;
            }

            bool ok = false;

            for (auto& r : ranges)
                ok = ok || (t->program >= r.first && t->program <= r.second);

            if (! ok)
                return false;
        }

        return true;
    };

    auto players = [&] (const Composition& c)
    {
        std::set<std::string> keys;

        for (auto* t : playing (c))
            keys.insert (instrumentKey (t->name, t->program, t->isDrum));

        return (int) keys.size();
    };

    auto hasProgram = [&] (const Composition& c, int lo, int hi)
    {
        for (auto* t : playing (c))
            if (! t->isDrum && t->program >= lo && t->program <= hi)
                return true;

        return false;
    };

    // Every one of these meets all its criteria, with several seeds.
    const char* prompts[] = {
        "Make me a soft piano midi", "a piano piece", "solo violin piece", "Sad piano ballad in D minor", "cello and piano duet",
        "flute melody only", "string quartet in G major", "fast techno track at 140 bpm", "orchestral piece without percussion",
        "30 second piano jingle", "waltz for piano", "jazz piano trio", "happy ukulele song", "dark ambient pad, no drums",
        "a short harp lullaby", "epic choir and organ", "rock song with no guitar", "slow sad cello solo",
        "Late night jazz combo with a tenor sax lead", "Lo-fi hip hop beat with a mellow electric piano, rainy night",
        "Peaceful orchestral village theme with woodwinds and harp", "16 bars of funky bass and drums", "string trio",
        "woodwind quintet", "trumpet and piano duet in Bb major", "a 45 second marimba loop", "just drums",
        "2 minute orchestral adventure theme in E minor", "calm acoustic guitar in 6/8", "Gentle lullaby for music box and harp",
    };

    for (auto* p : prompts)
    {
        for (uint64_t seed : { 3ull, 29ull })
        {
            const auto spec = interpretPrompt (p, seed);
            const auto c = compose (spec, seed);
            CHECK (c.request.total() > 0);

            if (! c.request.allMet())
            {
                std::printf ("  \"%s\" (seed %d) misses:", p, (int) seed);

                for (auto& cr : c.request.criteria)
                    if (! cr.met)
                        std::printf (" [%s: %s]", cr.label.c_str(), cr.detail.c_str());

                std::printf ("\n");
            }

            CHECK (c.request.allMet());
        }
    }

    // ...and the notes really are what was asked for.
    {
        const auto c = compose (interpretPrompt ("waltz for piano", 5), 5);
        CHECK (onlyPrograms (c, { { 0, 7 } }, false));
        CHECK_EQ (c.tsNum, 3);
    }
    {
        const auto c = compose (interpretPrompt ("jazz piano trio", 5), 5);
        CHECK_EQ (players (c), 3);
        CHECK (! hasProgram (c, 64, 67));        // no sax
        CHECK (hasProgram (c, 0, 7));
    }
    {
        const auto c = compose (interpretPrompt ("happy ukulele song", 5), 5);
        bool ukulele = false;

        for (auto* t : playing (c))
            ukulele = ukulele || t->name.find ("Ukulele") != std::string::npos;

        CHECK (ukulele);
        CHECK_EQ (players (c), 1);
    }
    {
        const auto c = compose (interpretPrompt ("orchestral piece without percussion", 5), 5);
        CHECK (onlyPrograms (c, { { 0, 8 }, { 10, 10 }, { 15, 46 }, { 48, 111 } }, false));   // no drums, timpani or mallets
        CHECK (c.title.find ("Percussion") == std::string::npos);
    }
    {
        const auto c = compose (interpretPrompt ("rock song with no guitar", 5), 5);
        CHECK (! hasProgram (c, 24, 31));
        CHECK (c.title.find ("Guitar") == std::string::npos);
    }
    {
        const auto c = compose (interpretPrompt ("epic choir and organ", 5), 5);
        CHECK (onlyPrograms (c, { { 16, 20 }, { 52, 54 } }, false));
    }
    {
        const auto c = compose (interpretPrompt ("30 second piano jingle", 5), 5);
        CHECK (std::abs (c.seconds() - 30.0) <= 3.6);
    }
    {
        const auto c = compose (interpretPrompt ("Sad piano ballad in D minor", 5), 5);
        CHECK_EQ (c.keyRoot, 2);
        CHECK (c.mode == Mode::Minor || c.mode == Mode::HarmonicMinor || c.mode == Mode::MelodicMinor);
    }
    {
        const auto c = compose (interpretPrompt ("Late night jazz combo with a tenor sax lead", 5), 5);
        bool saxLeads = false;

        for (auto* t : playing (c))
            saxLeads = saxLeads || (t->program == 66 && t->role == Role::Melody);

        CHECK (saxLeads);
    }
    {
        const auto c = compose (interpretPrompt ("16 bars of funky bass and drums", 5), 5);
        CHECK (onlyPrograms (c, { { 32, 39 } }, true));
        CHECK_EQ (c.bars, 16);
    }
    {
        const auto c = compose (interpretPrompt ("string trio", 5), 5);
        CHECK_EQ (players (c), 3);
        CHECK (onlyPrograms (c, { { 40, 51 } }, false));
    }

    // Instruments that join a style rather than replace it.
    {
        const auto spec = interpretPrompt ("Lo-fi hip hop beat with a mellow electric piano, rainy night", 5);
        CHECK (! spec.request.onlyThese);
        CHECK (spec.tracks.size() >= 3);
        const auto heroic = interpretPrompt ("Heroic main theme for an adventure game, triumphant brass and strings", 5);
        CHECK (! heroic.request.onlyThese);
        CHECK (heroic.tracks.size() >= 10);
    }

    // The check catches a piece that isn't what was asked for.
    {
        const auto spec = interpretPrompt ("a piano piece", 5);
        auto c = compose (spec, 5);
        Track drums;
        drums.name = "Drums";
        drums.isDrum = true;
        drums.channel = 9;

        for (int i = 0; i < 16; ++i)
            drums.notes.push_back ({ i * 480, 120, 36, 100 });

        c.tracks.push_back (drums);
        const auto report = checkRequest (c, spec.request);
        CHECK (! report.allMet());
        CHECK (report.criteria.front().label == "Only piano" && ! report.criteria.front().met);

        c.tempo = 200;
        Request fast = spec.request;
        fast.tempo = 90;
        CHECK (! checkRequest (c, fast).allMet());
    }

    // Your own instruments are the request.
    {
        PromptOverrides o;
        InstrumentChoice flute;
        flute.name = "Flute";
        flute.program = 73;
        o.instruments = { flute };
        const auto spec = interpretPrompt ("epic orchestral battle", 5, o);
        CHECK (spec.request.onlyThese);
        CHECK_EQ ((int) spec.request.instruments.size(), 1);
        CHECK (compose (spec, 5).request.allMet());
    }

    // The controls are criteria too.
    {
        PromptOverrides o;
        o.keyRoot = 9;
        o.tempo = 133;
        const auto c = compose (interpretPrompt ("happy pop song", 5, o), 5);
        CHECK_EQ (c.keyRoot, 9);
        CHECK_EQ (c.tempo, 133);
        CHECK (c.request.allMet());
    }
}

int main()
{
    testChordParsing();
    testPromptInterpretation();
    testComposer();
    testEveryStyleAndMeter();
    testPartsFollowTheHarmony();
    testThemeAndHandWrittenSpec();
    testCueFormAndLength();
    testCueQuality();
    testFullOrchestraAndArticulations();
    testThemeDevelopment();
    testLearnedModels();
    testVariationsDiffer();
    testCustomInstruments();
    testRequests();

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
