#include "MidiFileWriter.h"

#include <algorithm>
#include <string>

namespace pm
{

namespace
{
    struct Event
    {
        int tick;
        int order;  // at equal ticks: meta < control < note-off < note-on
        std::vector<uint8_t> bytes;
    };

    void putVarLen (std::vector<uint8_t>& out, uint32_t value)
    {
        uint8_t buffer[5];
        int n = 0;
        buffer[n++] = (uint8_t) (value & 0x7F);

        while ((value >>= 7) != 0)
            buffer[n++] = (uint8_t) ((value & 0x7F) | 0x80);

        while (n > 0)
            out.push_back (buffer[--n]);
    }

    void putBE32 (std::vector<uint8_t>& out, uint32_t v)
    {
        out.push_back ((uint8_t) (v >> 24));
        out.push_back ((uint8_t) (v >> 16));
        out.push_back ((uint8_t) (v >> 8));
        out.push_back ((uint8_t) v);
    }

    void putBE16 (std::vector<uint8_t>& out, uint16_t v)
    {
        out.push_back ((uint8_t) (v >> 8));
        out.push_back ((uint8_t) v);
    }

    std::vector<uint8_t> metaText (uint8_t type, const std::string& text)
    {
        std::vector<uint8_t> b { 0xFF, type };
        putVarLen (b, (uint32_t) text.size());
        b.insert (b.end(), text.begin(), text.end());
        return b;
    }

    void writeTrackChunk (std::vector<uint8_t>& file, std::vector<Event> events, int endTick)
    {
        std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b)
                          { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });

        std::vector<uint8_t> data;
        int last = 0;

        for (auto& e : events)
        {
            const int tick = std::max (e.tick, last);
            putVarLen (data, (uint32_t) (tick - last));
            data.insert (data.end(), e.bytes.begin(), e.bytes.end());
            last = tick;
        }

        putVarLen (data, (uint32_t) std::max (0, endTick - last));
        data.push_back (0xFF);
        data.push_back (0x2F);
        data.push_back (0x00);

        file.insert (file.end(), { 'M', 'T', 'r', 'k' });
        putBE32 (file, (uint32_t) data.size());
        file.insert (file.end(), data.begin(), data.end());
    }

    int log2Denominator (int den)
    {
        int p = 0;

        while ((1 << p) < den && p < 7)
            ++p;

        return p;
    }
}

ArticulationMap ArticulationMap::keyswitchDefaults()
{
    ArticulationMap m;
    m.output = ArticulationOutput::Keyswitches;
    //          Default Legato Sustain Staccato Spiccato Marcato Pizz Trem Trill
    m.values = { 12,     13,    12,     14,      15,      16,     17,  18,  19 };
    return m;
}

ArticulationMap ArticulationMap::uaccDefaults()
{
    ArticulationMap m;
    m.output = ArticulationOutput::Controller;
    m.controller = 32;
    m.values = { 1, 20, 1, 40, 42, 52, 56, 11, 71 };
    return m;
}

bool usesArticulations (const Track& t)
{
    if (t.isDrum)
        return false;

    const int p = t.program;
    return (p >= 40 && p <= 45) || (p >= 48 && p <= 51) || (p >= 56 && p <= 79);
}

std::vector<uint8_t> writeMidiFile (const Composition& comp, int onlyTrack, const MidiExportOptions& options)
{
    const auto& map = options.articulations;
    const bool switching = map.output != ArticulationOutput::None;
    std::vector<int> trackIndices;

    for (int i = 0; i < (int) comp.tracks.size(); ++i)
    {
        const auto& t = comp.tracks[(size_t) i];

        // Split parts travel inside their section when the library switches articulations.
        if (switching && t.splitFrom >= 0 && t.splitFrom < (int) comp.tracks.size() && onlyTrack != i)
            continue;

        if (onlyTrack < 0 || onlyTrack == i)
            trackIndices.push_back (i);
    }

    std::vector<uint8_t> file { 'M', 'T', 'h', 'd' };
    putBE32 (file, 6);
    putBE16 (file, 1);                                         // format 1
    putBE16 (file, (uint16_t) (trackIndices.size() + 1));      // + conductor track
    putBE16 (file, (uint16_t) comp.ppq);

    const int endTick = std::max (comp.lengthTicks, comp.ticksPerBar);

    // Conductor track
    {
        std::vector<Event> ev;
        ev.push_back ({ 0, 0, metaText (0x03, comp.title) });

        const uint32_t usPerQuarter = (uint32_t) (60000000.0 / std::max (1, comp.tempo));
        ev.push_back ({ 0, 0, { 0xFF, 0x51, 0x03, (uint8_t) (usPerQuarter >> 16), (uint8_t) (usPerQuarter >> 8), (uint8_t) usPerQuarter } });

        const uint8_t clocks = (uint8_t) (comp.tsDen == 8 ? 36 : 24);
        ev.push_back ({ 0, 0, { 0xFF, 0x58, 0x04, (uint8_t) comp.tsNum, (uint8_t) log2Denominator (comp.tsDen), clocks, 8 } });

        const int sf = keySignatureAccidentals (comp.keyRoot, comp.mode);
        const bool minor = comp.mode == Mode::Minor || comp.mode == Mode::HarmonicMinor
                        || comp.mode == Mode::MelodicMinor || comp.mode == Mode::PentatonicMinor;
        ev.push_back ({ 0, 0, { 0xFF, 0x59, 0x02, (uint8_t) (int8_t) sf, (uint8_t) (minor ? 1 : 0) } });

        if (options.markers)
        {
            for (auto& m : comp.markers)
                ev.push_back ({ m.tick, 0, metaText (0x06, m.text) });

            // Loop points also as cue points, which game audio tools read.
            if (comp.loopStart >= 0 && comp.loopEnd > comp.loopStart)
            {
                ev.push_back ({ comp.loopStart, 0, metaText (0x07, "loopStart") });
                ev.push_back ({ comp.loopEnd, 0, metaText (0x07, "loopEnd") });
            }
        }

        writeTrackChunk (file, ev, endTick);
    }

    for (int idx : trackIndices)
    {
        const auto& t = comp.tracks[(size_t) idx];
        const uint8_t ch = (uint8_t) (t.channel & 0x0F);
        std::vector<Event> ev;

        ev.push_back ({ 0, 0, metaText (0x03, t.name) });
        ev.push_back ({ 0, 0, metaText (0x04, t.isDrum ? std::string ("Drum Kit") : std::string (gmProgramName (t.program))) });

        if (! t.isDrum)
            ev.push_back ({ 0, 1, { (uint8_t) (0xC0 | ch), (uint8_t) (t.program & 0x7F) } });

        for (auto& c : t.controls)
            ev.push_back ({ c.tick, 2, { (uint8_t) (0xB0 | ch), (uint8_t) (c.controller & 0x7F), (uint8_t) (c.value & 0x7F) } });

        std::vector<Note> notes = t.notes;
        const bool articulate = switching && usesArticulations (t);

        if (switching && onlyTrack < 0)
        {
            for (auto& other : comp.tracks)
                if (other.splitFrom == idx)
                    notes.insert (notes.end(), other.notes.begin(), other.notes.end());

            std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });

            // Folded-in parts must not hold a pitch the section strikes again.
            for (size_t i = 0; i < notes.size(); ++i)
                for (size_t j = i + 1; j < notes.size() && notes[j].start < notes[i].start + notes[i].length; ++j)
                    if (notes[j].pitch == notes[i].pitch && notes[j].start > notes[i].start)
                        notes[i].length = std::max (1, notes[j].start - notes[i].start);
        }

        if (articulate)
        {
            // Announce each change of articulation just before the notes it applies to.
            int current = -2;
            int lastSwitch = -1000000;

            for (auto& n : notes)
            {
                Articulation a = n.articulation;

                if (a == Articulation::Default)
                    a = Articulation::Sustain;

                const int value = map.valueFor (a);

                if (value < 0 || value == current)
                    continue;

                const int at = std::max ({ 0, n.start - map.leadTicks, lastSwitch + 1 });

                if (map.output == ArticulationOutput::Keyswitches)
                {
                    ev.push_back ({ at, 2, { (uint8_t) (0x90 | ch), (uint8_t) (value & 0x7F), 100 } });
                    ev.push_back ({ std::min (at + 6, std::max (at + 1, n.start)), 2, { (uint8_t) (0x80 | ch), (uint8_t) (value & 0x7F), 64 } });
                }
                else
                {
                    ev.push_back ({ at, 2, { (uint8_t) (0xB0 | ch), (uint8_t) (map.controller & 0x7F), (uint8_t) (value & 0x7F) } });
                }

                current = value;
                lastSwitch = at;
            }
        }

        for (auto& n : notes)
        {
            ev.push_back ({ n.start, 4, { (uint8_t) (0x90 | ch), (uint8_t) (n.pitch & 0x7F), (uint8_t) (std::max (1, n.velocity) & 0x7F) } });
            ev.push_back ({ n.start + n.length, 3, { (uint8_t) (0x80 | ch), (uint8_t) (n.pitch & 0x7F), 64 } });
        }

        writeTrackChunk (file, ev, endTick);
    }

    return file;
}

} // namespace pm
