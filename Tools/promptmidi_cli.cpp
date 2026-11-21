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
    std::cout << "usage: promptmidi-cli \"<prompt>\" [-o file.mid] [--seed N] [--notes] [--list-styles]\n";
}

int main (int argc, char** argv)
{
    std::string prompt, outFile;
    uint64_t seed = 1;
    bool printNotes = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];

        if ((a == "-o" || a == "--output") && i + 1 < argc)   outFile = argv[++i];
        else if (a == "--seed" && i + 1 < argc)               seed = std::strtoull (argv[++i], nullptr, 10);
        else if (a == "--notes")                              printNotes = true;
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

    const auto spec = pm::interpretPrompt (prompt, seed);
    const auto comp = pm::compose (spec, seed);

    std::cout << "Title:   " << spec.title << "\n"
              << "Summary: " << comp.summary() << "\n";

    for (auto& line : spec.interpretation)
        std::cout << "  - " << line << "\n";

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

    const auto bytes = pm::writeMidiFile (comp);
    std::ofstream f (outFile, std::ios::binary);
    f.write ((const char*) bytes.data(), (std::streamsize) bytes.size());
    std::cout << "Wrote " << outFile << " (" << bytes.size() << " bytes)\n";
    return f.good() ? 0 : 2;
}
