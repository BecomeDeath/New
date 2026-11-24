#pragma once

#include "Composition.h"
#include "Judge.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace pm
{

/** How long PromptMIDI thinks before it answers.

    Quick answers at once (the first plan, best of a few drafts). Thorough and Deep read the
    prompt into several different plans (keys, tempos, harmony, orchestration within what was
    asked), write drafts of each, take the most promising plans further, and keep the draft the
    judge (Judge.h) rates highest among those that meet everything that was asked for. */
enum class ThinkingDepth { Quick = 0, Thorough = 1, Deep = 2 };

struct ThinkingBudget
{
    int plans = 1;           // different readings of the request
    int draftsPerPlan = 1;   // drafts of each, in the first round
    int focusPlans = 0;      // the best plans, developed further
    int focusDrafts = 0;     // more drafts of each of those
    int variants = 0;        // ... and of this many variations of each (other harmony, figures)

    int totalDrafts() const  { return plans * draftsPerPlan + focusPlans * focusDrafts * (1 + variants); }
};

ThinkingBudget budgetFor (ThinkingDepth);
const char* depthName (ThinkingDepth);

/** Where the thinking is, for the progress display. */
struct ThinkingProgress
{
    enum class Stage { Reading, Planning, Drafting, Developing, Checking, Done };

    Stage stage = Stage::Reading;
    int drafts = 0;            // composed so far
    int totalDrafts = 0;       // planned in all
    int plans = 0;             // sketched so far
    double best = 0.0;         // the best draft's judgement so far (0-100)
    int bestMet = 0;           // ... the criteria it meets
    int criteria = 0;          // ... out of
    double seconds = 0.0;
    std::string detail;        // a line about the current step
    std::string stageDetail[5];   // the latest line of each stage (Reading .. Checking)
};

const char* stageName (ThinkingProgress::Stage);

/** One reading of the request. */
struct ThinkingPlan
{
    StyleSpec spec;
    uint64_t planSeed = 0;       // what made it (an interpretation seed)
    uint64_t variationSeed = 0;  // and, for a developed plan, the variation of it (0 = none)
};

struct ThinkingRequest
{
    /** Makes plan i (0 .. budget.plans - 1). Must be deterministic; plan 0 is the first reading,
        the one Quick uses. Leave `plans` at 1 to keep a fixed plan (Claude's) and only draft. */
    std::function<ThinkingPlan (int index)> makePlan;

    /** Optional: a variation of a promising plan (same key, tempo, length and instruments, other
        harmony and figures), tried while developing the best ideas. */
    std::function<ThinkingPlan (const ThinkingPlan& base, uint64_t variationSeed)> varyPlan;

    uint64_t seed = 1;                         // everything below is derived from it
    ThinkingDepth depth = ThinkingDepth::Thorough;
    bool fixedPlan = false;                    // one plan only: all the drafts go to it
    std::optional<ThinkingBudget> budget;      // instead of the depth's (tools and tests)

    /** Earlier takes a new one should differ from (Variation); empty for a fresh Generate. */
    std::vector<Composition> otherTakes;
    double maxTakeOverlap = 0.35;

    /** Called after every step (on the thinking thread). */
    std::function<void (const ThinkingProgress&)> onProgress;

    /** 0 = keep thinking, 1 = finish now with the best so far, 2 = cancel. */
    const std::atomic<int>* control = nullptr;
};

struct ThinkingResult
{
    bool ok = false;                // false when cancelled before a single draft
    bool stoppedEarly = false;      // "use the best so far"
    Composition composition;
    StyleSpec spec;                 // the winning plan
    uint64_t planSeed = 0;          // ... and the seeds that reproduce it:
    uint64_t variationSeed = 0;     //     the plan (interpretation, then variation, if any)
    uint64_t composeSeed = 0;       //     compose (spec, composeSeed) == composition
    Judgement judgement;
    int planIndex = 0, draftIndex = 0;  // which draft won (1-based numbers in the text)
    int drafts = 0, plans = 0, meetingAll = 0;
    double seconds = 0.0;
    double takeOverlap = -1.0;      // with the closest earlier take (-1 = none compared)
    bool reproducible = true;       // composing the winner again from its seeds gave the same notes
    std::vector<std::string> reasoning;   // how it decided, line by line
};

/** Thinks: plans, drafts, develops the best, checks, and returns the chosen draft. Runs on the
    calling thread; deterministic for a given request (the time taken doesn't change the result,
    unless it is told to finish early). */
ThinkingResult think (const ThinkingRequest&);

/** The seed for draft d of plan i (plan 0, draft 0 is the request's own seed). */
uint64_t draftSeed (uint64_t seed, int plan, int draft);

/** The seed plan i is made from (plan 0 uses the request's own seed). */
uint64_t planSeedFor (uint64_t seed, int plan);

} // namespace pm
