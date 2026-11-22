// Command-line front end for the PromptMIDI engine (offline mode only).
//
//   promptmidi-cli "Generate me a fantasy orchestral midi" -o fantasy.mid
//   promptmidi-cli "soft piano in D minor, 70 bpm" --seed 7 --notes

#include "Composer.h"
#include "MidiFileWriter.h"
#include "PromptInterpreter.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

static void usage()
{
    std::cout << "usage: promptmidi-cli \"<prompt>\" [-o file.mid] [--seed N] [--notes] [--json file.json]\n"
                 "                     [--length sketch|cue|full|extended] [--articulations none|keyswitch|uacc]\n"
                 "                     [--variation N] [--instruments \"Piano\" | \"Flutes, Harp:arpeggio\"] [--list-styles]\n";
}

static std::string jsonEscape (const std::string& s)
{
    std::string out;

    for (unsigned char c : s)
    {
        if (c == '"' || c == '\\')  { out += '\\'; out += (char) c; }
        else if (c < 0x20)          { char buf[8]; std::snprintf (buf, sizeof (buf), "\\u%04x", c); out += buf; }
        else                        out += (char) c;
    }

    return out;
}

/** Dumps the composition (and how the prompt was read) for analysis tools. */
static void writeJson (const std::string& path, const pm::StyleSpec& spec, const pm::Composition& c)
{
    std::ofstream f (path);
    f << "{\"title\":\"" << jsonEscape (c.title) << "\",\"style\":\"" << spec.styleId << "\",\"tempo\":" << c.tempo
      << ",\"tsNum\":" << c.tsNum << ",\"tsDen\":" << c.tsDen << ",\"bars\":" << c.bars << ",\"ticksPerBar\":" << c.ticksPerBar
      << ",\"lengthTicks\":" << c.lengthTicks << ",\"keyRoot\":" << c.keyRoot << ",\"endKeyRoot\":" << c.endKeyRoot << ",\"mode\":\"" << pm::modeInfo (c.mode).id
      << "\",\"summary\":\"" << jsonEscape (c.summary()) << "\",\"interpretation\":[";

    for (size_t i = 0; i < spec.interpretation.size(); ++i)
        f << (i ? "," : "") << "\"" << jsonEscape (spec.interpretation[i]) << "\"";

    f << "],\"chords\":[";

    for (size_t i = 0; i < c.chords.size(); ++i)
    {
        pm::Chord ch;
        pm::parseChordSymbol (c.chords[i].name, 0, pm::Mode::Major, ch);
        f << (i ? "," : "") << "{\"start\":" << c.chords[i].start << ",\"end\":" << c.chords[i].end << ",\"name\":\""
          << jsonEscape (c.chords[i].name) << "\",\"roman\":\"" << jsonEscape (c.chords[i].roman) << "\",\"bassPc\":"
          << c.chords[i].bassPc << ",\"pcs\":[";

        auto pcs = c.chords[i].pcs.empty() ? ch.pitchClasses() : c.chords[i].pcs;

        for (size_t k = 0; k < pcs.size(); ++k)
            f << (k ? "," : "") << pcs[k];

        f << "]}";
    }

    f << "],\"markers\":[";

    for (size_t i = 0; i < c.markers.size(); ++i)
        f << (i ? "," : "") << "[" << c.markers[i].tick << ",\"" << jsonEscape (c.markers[i].text) << "\"]";

    f << "],\"loop\":[" << c.loopStart << "," << c.loopEnd << "],\"quality\":" << c.quality.score << ",\"tracks\":[";

    for (size_t t = 0; t < c.tracks.size(); ++t)
    {
        const auto& tr = c.tracks[t];
        f << (t ? "," : "") << "{\"name\":\"" << jsonEscape (tr.name) << "\",\"role\":\"" << pm::roleId (tr.role) << "\",\"program\":"
          << tr.program << ",\"channel\":" << tr.channel << ",\"drum\":" << (tr.isDrum ? "true" : "false")
          << ",\"entryBar\":" << (t < spec.tracks.size() ? spec.tracks[t].entryBar : 0) << ",\"cc\":" << tr.controls.size() << ",\"notes\":[";

        for (size_t n = 0; n < tr.notes.size(); ++n)
        {
            const auto& no = tr.notes[n];
            f << (n ? "," : "") << "[" << no.start << "," << no.length << "," << no.pitch << "," << no.velocity << "," << (int) no.articulation << "]";
        }

        f << "]}";
    }

    f << "]}\n";
}

int main (int argc, char** argv)
{
    std::string prompt, outFile, jsonFile, articulations = "none";
    uint64_t seed = 1, variation = 0;
    bool printNotes = false;
    pm::PromptOverrides overrides;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];

        if ((a == "-o" || a == "--output") && i + 1 < argc)   outFile = argv[++i];
        else if (a == "--seed" && i + 1 < argc)               seed = std::strtoull (argv[++i], nullptr, 10);
        else if (a == "--variation" && i + 1 < argc)          variation = std::strtoull (argv[++i], nullptr, 10);
        else if (a == "--notes")                              printNotes = true;
        else if (a == "--json" && i + 1 < argc)               jsonFile = argv[++i];
        else if (a == "--length" && i + 1 < argc)             overrides.length = argv[++i];
        else if (a == "--instruments" && i + 1 < argc)
        {
            // "Piano" or "Flutes, Harp:arpeggio, Celli:bass" - names from the catalogue, optional :role
            std::string list = argv[++i], item;

            auto flush = [&]
            {
                while (! item.empty() && item.front() == ' ') item.erase (item.begin());
                while (! item.empty() && item.back() == ' ') item.pop_back();

                if (item.empty())
                    return;

                pm::InstrumentChoice c;
                const auto colon = item.find (':');
                c.name = item.substr (0, colon);
                c.role = colon == std::string::npos ? "auto" : item.substr (colon + 1);

                for (auto& e : pm::instrumentCatalogue())
                    if (e.name == c.name)
                        c.program = e.program;

                overrides.instruments.push_back (c);
                item.clear();
            };

            for (char ch : list)
            {
                if (ch == ',') flush();
                else item += ch;
            }

            flush();
        }
        else if (a == "--articulations" && i + 1 < argc)      articulations = argv[++i];
        else if (a == "--list-styles")
        {
            for (auto& s : pm::builtInStyles())
                std::cout << s.id << "  -  " << s.displayName << "\n";

            return 0;
        }
        else if (a == "-h" || a == "--help")                  { usage(); return 0; }
        else                                                  prompt += (prompt.empty() ? "" : " ") + a;
    }

    if (prompt.empty())
    {
        usage();
        return 1;
    }

    auto spec = pm::interpretPrompt (prompt, seed, overrides);

    if (variation > 0)
        spec = pm::varySpec (spec, seed * 1000003ull + variation);

    // --variation N: the same plan, composed again with another seed (the plug-in's Variation button).
    const auto comp = pm::compose (spec, variation > 0 ? seed * 1000003ull + variation : seed);

    std::cout << "Title:   " << spec.title << "\n"
              << "Summary: " << comp.summary() << "\n";

    for (auto& line : spec.interpretation)
        std::cout << "  - " << line << "\n";

    if (! comp.form.empty())
        std::cout << "Form:    " << comp.form << "\n";

    std::printf ("Length:  %d bars, %d:%02d\n", comp.bars, (int) comp.seconds() / 60, (int) comp.seconds() % 60);
    std::printf ("Quality: %.1f / 10 (best of %d drafts)\n", comp.quality.score, comp.quality.candidates);

    if (comp.request.total() > 0)
    {
        std::printf ("Request: %d of %d criteria met\n", comp.request.met(), comp.request.total());

        for (auto& c : comp.request.criteria)
            std::cout << "  " << (c.met ? "[x] " : "[ ] ") << c.label << (c.met ? std::string() : " - " + c.detail) << "\n";
    }

    for (auto& q : comp.quality.notesText)
        std::cout << "  - " << q << "\n";

    std::cout << "Tracks:\n";

    for (auto& t : comp.tracks)
    {
        std::printf ("  ch%-2d %-26s %-22s %4d notes\n", t.channel + 1, t.name.c_str(),
                     t.isDrum ? "Drum Kit" : pm::gmProgramName (t.program), (int) t.notes.size());

        if (printNotes)
            for (auto& n : t.notes)
                std::printf ("        t=%6d len=%5d pitch=%3d vel=%3d\n", n.start, n.length, n.pitch, n.velocity);
    }

    if (outFile.empty())
    {
        outFile = spec.title;

        for (auto& c : outFile)
            if (! std::isalnum ((unsigned char) c))
                c = '_';

        outFile += ".mid";
    }

    if (! jsonFile.empty())
        writeJson (jsonFile, spec, comp);

    pm::MidiExportOptions options;

    if (articulations == "keyswitch" || articulations == "keyswitches")  options.articulations = pm::ArticulationMap::keyswitchDefaults();
    else if (articulations == "uacc" || articulations == "cc")           options.articulations = pm::ArticulationMap::uaccDefaults();

    const auto bytes = pm::writeMidiFile (comp, -1, options);
    std::ofstream f (outFile, std::ios::binary);
    f.write ((const char*) bytes.data(), (std::streamsize) bytes.size());
    std::cout << "Wrote " << outFile << " (" << bytes.size() << " bytes)\n";
    return f.good() ? 0 : 2;
}
