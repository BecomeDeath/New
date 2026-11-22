#include "PresetIO.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace
{
    std::string toStd (const juce::String& s) { return s.toStdString(); }
}

juce::File PresetIO::presetsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("PromptMIDI").getChildFile ("Presets");
}

juce::Array<juce::File> PresetIO::userPresets()
{
    auto files = presetsFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + presetExtension);
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0; });
    return files;
}

const std::vector<PresetIO::Factory>& PresetIO::factoryPresets()
{
    static const std::vector<Factory> list = {
        { "Fantasy Adventure Cue",  "Generate me a fantasy orchestral midi", "full" },
        { "Boss Battle",            "Epic orchestral battle music for a boss fight", "full" },
        { "Villain's Castle",       "Dark fantasy orchestral theme for a villain's castle", "" },
        { "Village Morning",        "Peaceful orchestral village theme with woodwinds and harp", "" },
        { "Trailer Build",          "Cinematic trailer music building to a huge climax", "cue" },
        { "Menu Loop",              "Calm loopable menu music for a fantasy RPG", "cue" },
        { "Solo Piano Ballad",      "Sad piano ballad", "" },
        { "Soft Piano",             "Make me a soft piano midi", "" },
        { "String Quartet",         "String quartet in G major", "" },
        { "Lullaby",                "Gentle lullaby for music box and harp", "" },
        { "Lo-fi Study Beat",       "Lo-fi hip hop beat with a mellow electric piano, rainy night", "" },
        { "Late Night Jazz",        "Late night jazz combo with a tenor sax lead", "" },
        { "Jazz Piano Trio",        "Jazz piano trio", "" },
        { "Synthwave Drive",        "80s synthwave night drive with arpeggios", "" },
        { "Celtic Tavern",          "Lively celtic tavern jig with fiddle and whistle", "" },
        { "Ukulele Song",           "Happy ukulele song", "" },
    };

    return list;
}

//==============================================================================
void PresetIO::detectKey (const pm::Composition& c, int& keyRoot, bool& minor)
{
    static const double major[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double minorP[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    double weight[12] = {};

    for (auto& t : c.tracks)
        if (! t.isDrum)
            for (auto& n : t.notes)
                weight[n.pitch % 12] += std::min (n.length, 4 * pm::kPpq);

    auto correlate = [&] (const double* profile, int tonic)
    {
        double mx = 0, my = 0;

        for (int i = 0; i < 12; ++i)
        {
            mx += weight[(i + tonic) % 12];
            my += profile[i];
        }

        mx /= 12.0;
        my /= 12.0;
        double num = 0, dx = 0, dy = 0;

        for (int i = 0; i < 12; ++i)
        {
            const double x = weight[(i + tonic) % 12] - mx, y = profile[i] - my;
            num += x * y;
            dx += x * x;
            dy += y * y;
        }

        return dx > 0 && dy > 0 ? num / std::sqrt (dx * dy) : -1.0;
    };

    double best = -2.0;
    keyRoot = 0;
    minor = false;

    for (int tonic = 0; tonic < 12; ++tonic)
    {
        for (int m = 0; m < 2; ++m)
        {
            const double r = correlate (m == 0 ? major : minorP, tonic);

            if (r > best)
            {
                best = r;
                keyRoot = tonic;
                minor = m == 1;
            }
        }
    }
}

bool PresetIO::readMidiFile (const juce::File& file, pm::Composition& out, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "Couldn't find " + file.getFullPathName();
        return false;
    }

    if (file.getSize() > 16 * 1024 * 1024)
    {
        error = file.getFileName() + " is too large to import (over 16 MB).";
        return false;
    }

    juce::FileInputStream in (file);
    juce::MidiFile mf;

    if (! in.openedOk() || ! mf.readFrom (in, true))
    {
        error = file.getFileName() + " isn't a MIDI file PromptMIDI can read.";
        return false;
    }

    // Tempo, meter and key from the first events of each kind.
    juce::MidiMessageSequence tempos, sigs, keys;
    mf.findAllTempoEvents (tempos);
    mf.findAllTimeSigEvents (sigs);
    mf.findAllKeySigEvents (keys);

    double bpm = 120.0;
    std::set<int> distinctTempos;

    for (int i = 0; i < tempos.getNumEvents(); ++i)
    {
        const double spq = tempos.getEventPointer (i)->message.getTempoSecondsPerQuarterNote();

        if (spq > 0.0)
        {
            if (distinctTempos.empty())
                bpm = 60.0 / spq;

            distinctTempos.insert ((int) std::lround (60.0 / spq));
        }
    }

    bpm = juce::jlimit (20.0, 300.0, bpm);
    int num = 4, den = 4;

    if (sigs.getNumEvents() > 0)
    {
        sigs.getEventPointer (0)->message.getTimeSignatureInfo (num, den);
        num = juce::jlimit (1, 16, num);
        den = (den == 2 || den == 4 || den == 8 || den == 16) ? den : 4;
    }

    // One tempo: keep the file's grid exactly. Tempo changes: keep the timing, on the first tempo's grid.
    const short timeFormat = mf.getTimeFormat();
    const bool viaSeconds = timeFormat <= 0 || distinctTempos.size() > 1;
    const double tickScale = viaSeconds ? 0.0 : (double) pm::kPpq / (double) timeFormat;

    if (viaSeconds)
        mf.convertTimestampTicksToSeconds();

    auto toTicks = [&] (double t) { return (int) std::lround (viaSeconds ? t * bpm / 60.0 * pm::kPpq : t * tickScale); };

    out = pm::Composition();
    out.title = toStd (file.getFileNameWithoutExtension());
    out.tempo = (int) std::lround (bpm);
    out.tsNum = num;
    out.tsDen = den;
    out.ticksPerBar = juce::jmax (1, num * 4 * pm::kPpq / den);

    int lastTick = 0, totalNotes = 0;

    for (int ti = 0; ti < mf.getNumTracks() && (int) out.tracks.size() < 64; ++ti)
    {
        auto seq = *mf.getTrack (ti);
        seq.updateMatchedPairs();
        juce::String trackName;

        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            const auto& m = seq.getEventPointer (i)->message;

            if (m.isTrackNameEvent() && trackName.isEmpty())
                trackName = m.getTextFromTextMetaEvent().trim();

            if (m.isTextMetaEvent() && (m.getMetaEventType() == 6 || m.getMetaEventType() == 7))
                out.markers.push_back ({ toTicks (m.getTimeStamp()), toStd (m.getTextFromTextMetaEvent().trim()) });
        }

        std::map<int, pm::Track> byChannel;

        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            const auto* ev = seq.getEventPointer (i);
            const auto& m = ev->message;
            const int ch = m.getChannel();

            if (ch < 1 || ch > 16)
                continue;

            auto& t = byChannel[ch];

            if (m.isProgramChange() && t.notes.empty())
            {
                t.program = m.getProgramChangeNumber();
            }
            else if (m.isNoteOn() && ev->noteOffObject != nullptr && totalNotes < 200000)
            {
                pm::Note n;
                n.start = juce::jmax (0, toTicks (m.getTimeStamp()));
                n.length = juce::jmax (10, toTicks (ev->noteOffObject->message.getTimeStamp()) - n.start);
                n.pitch = m.getNoteNumber();
                n.velocity = juce::jlimit (1, 127, (int) m.getVelocity());
                t.notes.push_back (n);
                lastTick = juce::jmax (lastTick, n.start + n.length);
                ++totalNotes;
            }
            else if (m.isController())
            {
                const int cc = m.getControllerNumber();

                if (cc == 1 || cc == 7 || cc == 10 || cc == 11 || cc == 64)
                    t.controls.push_back ({ juce::jmax (0, toTicks (m.getTimeStamp())), cc, m.getControllerValue() });
            }
        }

        const bool several = std::count_if (byChannel.begin(), byChannel.end(), [] (const auto& p) { return ! p.second.notes.empty(); }) > 1;

        for (auto& [ch, t] : byChannel)
        {
            if (t.notes.empty() || (int) out.tracks.size() >= 64)
                continue;

            t.channel = ch - 1;
            t.isDrum = ch == 10;
            juce::String name = trackName;

            if (name.isEmpty())
                name = t.isDrum ? juce::String ("Drums") : juce::String (pm::gmProgramName (t.program));
            else if (several)
                name << " (ch " << ch << ")";

            t.name = toStd (name);
            std::sort (t.notes.begin(), t.notes.end(), [] (const pm::Note& a, const pm::Note& b) { return a.start < b.start; });
            out.tracks.push_back (std::move (t));
        }
    }

    if (totalNotes == 0)
    {
        error = file.getFileName() + " has no notes.";
        return false;
    }

    out.bars = juce::jmax (1, (lastTick + out.ticksPerBar - 1) / out.ticksPerBar);
    out.lengthTicks = out.bars * out.ticksPerBar;

    // Roles, for the arrangement view and for composing a Variation for the same instruments:
    // the high, mostly single-note part carries the tune, the lowest is the bass.
    int melody = -1, bass = -1;
    double bestMelody = -1e9, lowest = 1e9;

    for (int i = 0; i < (int) out.tracks.size(); ++i)
    {
        auto& t = out.tracks[(size_t) i];

        if (t.isDrum)
        {
            t.role = pm::Role::Percussion;
            continue;
        }

        double mean = 0.0;
        std::map<int, int> onsets;

        for (auto& n : t.notes)
        {
            mean += n.pitch;
            ++onsets[n.start / 60];
        }

        mean /= (double) t.notes.size();
        int single = 0;

        for (auto& o : onsets)
            single += o.second == 1 ? 1 : 0;

        const double mono = (double) single / (double) juce::jmax ((size_t) 1, onsets.size());
        t.role = mono < 0.6 ? pm::Role::Chords : pm::Role::CounterMelody;

        if (mono >= 0.6 && mean + 25.0 * mono > bestMelody)
        {
            bestMelody = mean + 25.0 * mono;
            melody = i;
        }

        if (mean < lowest)
        {
            lowest = mean;
            bass = i;
        }
    }

    if (bass >= 0 && lowest < 52.0 && bass != melody)
        out.tracks[(size_t) bass].role = pm::Role::Bass;

    if (melody >= 0)
        out.tracks[(size_t) melody].role = pm::Role::Melody;

    // Key: from the key signature, or from the notes.
    int keyRoot = 0;
    bool minor = false;

    if (keys.getNumEvents() > 0)
    {
        const auto& m = keys.getEventPointer (0)->message;
        const int sf = m.getKeySignatureNumberOfSharpsOrFlats();
        minor = ! m.isKeySignatureMajorKey();
        keyRoot = (((7 * sf - (minor ? 3 : 0)) % 12) + 12) % 12;
    }
    else
    {
        detectKey (out, keyRoot, minor);
    }

    out.keyRoot = out.endKeyRoot = keyRoot;
    out.mode = minor ? pm::Mode::Minor : pm::Mode::Major;
    return true;
}
