// Unit tests for the engine (no JUCE needed). Run with ctest or ./promptmidi_tests

#include "Composer.h"
#include "MidiFileWriter.h"
#include "PromptInterpreter.h"

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
    CHECK_EQ (a.lengthTicks, spec.bars * 4 * kPpq);

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
                if (t.role != Role::Chords && t.role != Role::Pad)
                    continue;

                int wrong = 0, total = 0;

                for (auto& n : t.notes)
                {
                    for (auto& slot : comp.chords)
                    {
                        if (n.start + 40 >= slot.start && n.start + 40 < slot.end)
                        {
                            Chord ch;
                            parseChordSymbol (slot.name, 0, Mode::Major, ch);
                            ++total;
                            wrong += ch.containsPc (n.pitch) ? 0 : 1;
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

int main()
{
    testChordParsing();
    testPromptInterpretation();
    testComposer();
    testEveryStyleAndMeter();
    testPartsFollowTheHarmony();
    testThemeAndHandWrittenSpec();

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
