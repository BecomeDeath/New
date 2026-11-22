#pragma once

#include <string>
#include <utility>
#include <vector>

namespace pm
{

/** An instrument the request names: "piano", "a tenor sax lead", "no drums". */
struct InstrumentNeed
{
    std::string name;                               // "Piano", "Drums", "Strings"
    std::vector<std::pair<int, int>> programs;      // General MIDI programs that count as this instrument
    std::vector<std::string> words;                 // track names that count too ("violin" matches "Violins I")
    int program = 0;                                // the program to use when it has to be added (-1 = drums)
    bool percussion = false;                        // drum kit / unpitched percussion tracks count
    bool lead = false;                              // it must carry the melody ("tenor sax lead")
};

/** What the user asked for, as criteria a finished piece can be checked against.

    Built from the prompt and the plug-in's controls (parseRequest in PromptInterpreter.h). The
    plan is made to meet it (applyRequest) and every generation is checked against it
    (checkRequest); takes that miss are composed again with other seeds.
*/
struct Request
{
    std::vector<InstrumentNeed> instruments;   // asked for: must be heard
    std::vector<InstrumentNeed> excluded;      // asked to leave out: must not be heard
    bool onlyThese = false;                    // nothing but `instruments` ("a piano piece", "for cello and piano")
    int ensembleSize = 0;                      // duo 2, trio 3, quartet 4, quintet 5 (0 = any)
    int keyRoot = -1;                          // 0-11
    int mode = -1;                             // an exact Mode
    int modeFeel = 0;                          // -1 minor-sounding ("sad", "minor"), +1 major-sounding ("happy", "major")
    int tempo = 0;                             // exact BPM
    int tempoMin = 0, tempoMax = 0;            // "slow" / "fast" (0 = no limit)
    int bars = 0;
    double seconds = 0.0;                      // "30 seconds", "2 minutes"
    double minSeconds = 0.0, maxSeconds = 0.0; // "short", "long", the Length control
    int tsNum = 0, tsDen = 0;                  // "waltz", "3/4", "jig"
    int loudness = 0;                          // -1 soft, +1 loud
    bool original = true;                      // the tune must be new (see Originality.h)

    bool hasInstrumentCriteria() const         { return ! instruments.empty() || ! excluded.empty() || onlyThese || ensembleSize > 0; }
};

/** One criterion and whether the piece meets it. */
struct CriterionResult
{
    std::string label;     // "Only piano", "Tenor sax plays the melody", "D minor", "About 30 seconds"
    bool met = true;
    std::string detail;    // what was found, when it isn't met
};

struct RequestReport
{
    std::vector<CriterionResult> criteria;

    int total() const                          { return (int) criteria.size(); }
    int met() const
    {
        int n = 0;

        for (auto& c : criteria)
            n += c.met ? 1 : 0;

        return n;
    }

    bool allMet() const                        { return met() == total(); }
};

} // namespace pm
