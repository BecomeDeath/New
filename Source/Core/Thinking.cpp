#include "Thinking.h"

#include "Composer.h"
#include "Originality.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <map>
#include <set>

namespace pm
{

ThinkingBudget budgetFor (ThinkingDepth d)
{
    switch (d)
    {
        case ThinkingDepth::Quick:    return { 1, 1, 0, 0, 0 };
        case ThinkingDepth::Thorough: return { 16, 8, 4, 14, 2 };    // 296 drafts
        case ThinkingDepth::Deep:     return { 32, 12, 8, 24, 2 };   // 960 drafts
    }

    return { 1, 1, 0, 0, 0 };
}

const char* depthName (ThinkingDepth d)
{
    switch (d)
    {
        case ThinkingDepth::Quick:    return "Quick";
        case ThinkingDepth::Thorough: return "Thorough";
        case ThinkingDepth::Deep:     return "Deep";
    }

    return "Thorough";
}

const char* stageName (ThinkingProgress::Stage s)
{
    switch (s)
    {
        case ThinkingProgress::Stage::Reading:    return "Reading your prompt";
        case ThinkingProgress::Stage::Planning:   return "Sketching plans";
        case ThinkingProgress::Stage::Drafting:   return "Writing drafts";
        case ThinkingProgress::Stage::Developing: return "Developing the best ideas";
        case ThinkingProgress::Stage::Checking:   return "Final check";
        case ThinkingProgress::Stage::Done:       return "Done";
    }

    return "";
}

namespace
{
uint64_t mix (uint64_t x)
{
    // splitmix64
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    x ^= x >> 31;
    return x == 0 ? 1 : x;
}

std::string planLabel (const StyleSpec& s)
{
    std::string text = keyName (s.keyRoot, s.mode) + ", " + std::to_string (s.tempo) + " BPM";

    if (! s.progression.empty())
    {
        text += ", ";

        for (size_t i = 0; i < s.progression.size() && i < 4; ++i)
            text += (i > 0 ? "-" : "") + s.progression[i];
    }

    return text;
}

std::string pct (double v) { return std::to_string ((int) std::lround (v)); }

std::string one (double v)
{
    char text[16];
    std::snprintf (text, sizeof (text), "%.1f", v);
    return text;
}

struct Draft
{
    int plan = 0, draft = 0;     // indices
    uint64_t seed = 0;
    int met = 0, criteria = 0;   // the request's criteria (and freshness, when compared)
    double score = 0.0;          // judgement 0-100
    double overlap = -1.0;
};

/** Better = meets more of what was asked; then the higher judgement; then the earlier draft (so
    the result never depends on timing). */
bool better (const Draft& a, const Draft& b)
{
    if (a.met != b.met)
        return a.met > b.met;

    if (std::abs (a.score - b.score) > 1e-9)
        return a.score > b.score;

    return a.plan != b.plan ? a.plan < b.plan : a.draft < b.draft;
}
} // namespace

uint64_t planSeedFor (uint64_t seed, int plan)
{
    return plan == 0 ? seed : mix (seed ^ (0xA24BAED4963EE407ull * (uint64_t) plan));
}

uint64_t draftSeed (uint64_t seed, int plan, int draft)
{
    if (plan == 0 && draft == 0)
        return seed;

    return mix (planSeedFor (seed, plan) + 0x632BE59BD9B4E019ull * (uint64_t) (draft + 1));
}

ThinkingResult think (const ThinkingRequest& req)
{
    using clock = std::chrono::steady_clock;
    const auto started = clock::now();
    auto elapsed = [&] { return std::chrono::duration<double> (clock::now() - started).count(); };
    auto control = [&] { return req.control != nullptr ? req.control->load() : 0; };

    ThinkingResult result;
    auto budget = req.budget ? *req.budget : budgetFor (req.depth);

    if (! req.varyPlan)
    {
        // Nothing to vary: the variations' drafts go to the focus plans themselves.
        budget.focusDrafts *= 1 + budget.variants;
        budget.variants = 0;
    }

    if (req.fixedPlan)
    {
        // One plan (Claude's): every first-round draft goes to it, then to its variations.
        const int develop = budget.focusPlans * budget.focusDrafts * (1 + budget.variants);
        budget.draftsPerPlan *= budget.plans;
        budget.plans = 1;

        if (budget.variants > 0 && budget.focusPlans > 0)
        {
            budget.focusPlans = 1;
            budget.focusDrafts = develop / (1 + budget.variants);
        }
        else
        {
            budget.draftsPerPlan += develop;
            budget.focusPlans = budget.focusDrafts = budget.variants = 0;
        }
    }

    ThinkingProgress progress;
    progress.totalDrafts = budget.totalDrafts();

    auto report = [&] (ThinkingProgress::Stage stage, const std::string& detail)
    {
        progress.stage = stage;
        progress.detail = detail;
        progress.seconds = elapsed();

        if ((int) stage < 5)
            progress.stageDetail[(int) stage] = detail;

        if (req.onProgress)
            req.onProgress (progress);
    };

    // 1. Read the request (plan 0 is the first, literal reading).
    std::vector<ThinkingPlan> plans;
    plans.push_back (req.makePlan (0));
    report (ThinkingProgress::Stage::Reading, planLabel (plans[0].spec) + ", " + std::to_string (plans[0].spec.tsNum) + "/"
                                                  + std::to_string (plans[0].spec.tsDen));
    progress.plans = 1;

    // 2. Sketch other readings: other keys, tempos, progressions and orchestrations within what
    //    was asked for (the request's own criteria are enforced in every plan).
    std::set<std::string> seen { planLabel (plans[0].spec) };

    for (int i = 1; i < budget.plans && control() != 2; ++i)
    {
        plans.push_back (req.makePlan (i));
        seen.insert (planLabel (plans.back().spec));
        progress.plans = (int) plans.size();
        report (ThinkingProgress::Stage::Planning, "plan " + std::to_string (i + 1) + ": " + planLabel (plans.back().spec));
    }

    if (control() == 2)
        return result;

    if (plans.size() > 1)
        report (ThinkingProgress::Stage::Planning, std::to_string (plans.size()) + " plans, " + std::to_string (seen.size())
                                                       + " different keys, tempos and progressions");

    // 3. Draft. Round-robin across the plans, so "use the best so far" has seen all of them.
    std::vector<Draft> drafts;
    std::map<int, Draft> bestOfPlan;
    Draft best;
    bool haveBest = false;
    Composition bestPiece;
    Judgement bestJudgement;

    auto write = [&] (int planIndex, int draftIndex, ThinkingProgress::Stage stage)
    {
        const auto& plan = plans[(size_t) planIndex];
        Draft d;
        d.plan = planIndex;
        d.draft = draftIndex;
        d.seed = draftSeed (req.seed, planIndex, draftIndex);

        Composition c = compose (plan.spec, d.seed);
        d.met = c.request.met();
        d.criteria = c.request.total();

        if (! req.otherTakes.empty())
        {
            double most = 0.0;

            for (auto& t : req.otherTakes)
                most = std::max (most, melodicOverlap (c, t));

            d.overlap = most;
            d.met += most < req.maxTakeOverlap ? 1 : 0;
            ++d.criteria;
        }

        const auto j = judgeDraft (c, plan.spec, d.overlap);
        d.score = j.total;
        drafts.push_back (d);

        if (bestOfPlan.count (planIndex) == 0 || better (d, bestOfPlan[planIndex]))
            bestOfPlan[planIndex] = d;

        if (! haveBest || better (d, best))
        {
            best = d;
            haveBest = true;
            bestPiece = std::move (c);
            bestJudgement = j;
        }

        progress.drafts = (int) drafts.size();
        progress.best = best.score;
        progress.bestMet = best.met;
        progress.criteria = best.criteria;
        report (stage, "draft " + std::to_string (drafts.size()) + " of " + std::to_string (progress.totalDrafts)
                         + " (plan " + std::to_string (planIndex + 1) + "): " + pct (d.score) + "/100"
                         + (d.met < d.criteria ? ", misses " + std::to_string (d.criteria - d.met) : std::string()));
    };

    auto stop = [&] { return control() != 0 && haveBest; };

    for (int k = 0; k < budget.draftsPerPlan && ! stop(); ++k)
        for (int i = 0; i < (int) plans.size() && ! stop(); ++i)
            write (i, k, ThinkingProgress::Stage::Drafting);

    if (control() == 2)
        return result;

    // 4. Develop the most promising plans with more drafts.
    std::vector<int> ranked;
    for (auto& p : bestOfPlan)
        ranked.push_back (p.first);

    std::sort (ranked.begin(), ranked.end(), [&] (int a, int b) { return better (bestOfPlan[a], bestOfPlan[b]); });
    const int focus = std::min (budget.focusPlans, (int) ranked.size());

    if (focus > 0 && ! stop())
    {
        std::string which;
        for (int f = 0; f < focus; ++f)
            which += (f == 0 ? "" : f == focus - 1 ? " and " : ", ") + std::to_string (ranked[(size_t) f] + 1);

        report (ThinkingProgress::Stage::Developing, "taking plan" + std::string (focus > 1 ? "s " : " ") + which + " further");
    }

    // Each focus plan gets more drafts, and so do variations of it (other harmony and figures).
    std::vector<int> developing;
    std::vector<int> developedFrom (plans.size(), -1);

    for (int f = 0; f < focus; ++f)
    {
        developing.push_back (ranked[(size_t) f]);

        for (int v = 0; v < budget.variants && req.varyPlan && control() != 2; ++v)
        {
            const auto& base = plans[(size_t) ranked[(size_t) f]];
            plans.push_back (req.varyPlan (base, mix (base.planSeed + 0x5851F42D4C957F2Dull * (uint64_t) (v + 1))));
            developing.push_back ((int) plans.size() - 1);
            developedFrom.push_back (ranked[(size_t) f]);
            progress.plans = (int) plans.size();
        }
    }

    for (int k = 0; k < budget.focusDrafts && ! stop(); ++k)
        for (size_t f = 0; f < developing.size() && ! stop(); ++f)
        {
            const int planIndex = developing[f];
            const bool original = planIndex < budget.plans;
            write (planIndex, original ? budget.draftsPerPlan + k : k, ThinkingProgress::Stage::Developing);
        }

    if (control() == 2 || ! haveBest)
        return result;

    // 5. Final check: the winner must come out the same when composed again (so the controls,
    //    Lock and saved projects reproduce it), and is checked once more as a whole.
    report (ThinkingProgress::Stage::Checking, "your request, originality, craft");
    const auto& plan = plans[(size_t) best.plan];

    {
        // Composed again from its seeds, it must come out note for note the same.
        const auto again = compose (plan.spec, best.seed);
        bool same = again.tracks.size() == bestPiece.tracks.size();

        for (size_t t = 0; same && t < again.tracks.size(); ++t)
        {
            const auto& a = again.tracks[t].notes;
            const auto& b = bestPiece.tracks[t].notes;
            same = a.size() == b.size();

            for (size_t n = 0; same && n < a.size(); ++n)
                same = a[n].start == b[n].start && a[n].pitch == b[n].pitch && a[n].length == b[n].length;
        }

        if (! same)
        {
            bestPiece = again;   // what the seeds reproduce is what you get
            bestJudgement = judgeDraft (bestPiece, plan.spec, best.overlap);
        }

        result.reproducible = same;
    }

    result.ok = true;
    result.stoppedEarly = control() == 1 && (int) drafts.size() < budget.totalDrafts();
    result.composition = std::move (bestPiece);
    result.spec = plan.spec;
    result.planSeed = plan.planSeed;
    result.variationSeed = plan.variationSeed;
    result.composeSeed = best.seed;
    result.judgement = bestJudgement;
    result.planIndex = best.plan;
    result.draftIndex = (int) (std::find_if (drafts.begin(), drafts.end(), [&] (const Draft& d) { return d.plan == best.plan && d.draft == best.draft; }) - drafts.begin());
    result.drafts = (int) drafts.size();
    result.plans = (int) plans.size();
    result.takeOverlap = best.overlap;

    for (auto& d : drafts)
        result.meetingAll += d.met == d.criteria ? 1 : 0;

    // How it decided, in a few lines.
    auto& why = result.reasoning;
    const int sketched = (int) developedFrom.size() - (int) std::count_if (developedFrom.begin(), developedFrom.end(), [] (int o) { return o >= 0; });
    const int variations = result.plans - sketched;
    const auto& first = plans[0].spec;
    why.push_back ("First reading of your prompt: " + keyName (first.keyRoot, first.mode) + ", " + std::to_string (first.tempo) + " BPM, "
                   + std::to_string (first.tsNum) + "/" + std::to_string (first.tsDen) + ".");

    if (sketched > 1)
        why.push_back ("Sketched " + std::to_string (sketched) + " plans (" + std::to_string (seen.size()) + " different keys, tempos and progressions)"
                       + (variations > 0 ? ", then " + std::to_string (variations) + " variations of the " + std::to_string (focus) + " best." : std::string (".")));

    why.push_back ("Wrote " + std::to_string (result.drafts) + " drafts; " + std::to_string (result.meetingAll) + " met everything you asked for"
                   + (result.stoppedEarly ? " (stopped early, as you asked)." : "."));

    if (result.drafts > 1)
    {
        const int from = best.plan < (int) developedFrom.size() ? developedFrom[(size_t) best.plan] : -1;
        const std::string which = result.plans <= 1 ? std::string()
                                : from >= 0 ? ", from a variation of plan " + std::to_string (from + 1)
                                : ", from plan " + std::to_string (best.plan + 1);
        why.push_back ("Chose draft " + std::to_string (result.draftIndex + 1) + which + " (" + planLabel (plan.spec) + "): " + pct (best.score) + "/100.");
    }
    else
        why.push_back ("Judged it " + pct (best.score) + "/100.");

    // Each aspect of the chosen draft.
    for (auto& a : bestJudgement.aspects)
        if (a.name != "Your request" || a.score < 10.0)
            why.push_back ("  " + a.name + " " + one (a.score) + ": " + a.note);

    if (result.drafts > 1)
    {
        double median = 0.0;
        std::vector<double> scores;
        for (auto& d : drafts)
            scores.push_back (d.score);

        std::sort (scores.begin(), scores.end());
        median = scores[scores.size() / 2];
        why.push_back ("The typical draft scored " + pct (median) + "/100, the weakest " + pct (scores.front()) + ".");
    }

    progress.stage = ThinkingProgress::Stage::Done;
    result.seconds = elapsed();
    progress.seconds = result.seconds;
    progress.detail = "chose draft " + std::to_string (result.draftIndex + 1);

    if (req.onProgress)
        req.onProgress (progress);

    return result;
}

} // namespace pm
