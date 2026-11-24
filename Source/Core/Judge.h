#pragma once

#include "Composition.h"

#include <string>
#include <vector>

namespace pm
{

/** A musician's read of a finished draft. The quality check (QualityCheck.h) finds mistakes; the
    judge weighs drafts that have none against each other, the way you would listen back to several
    takes and keep the best: does the tune go somewhere and come home, does the harmony move and
    cadence, can you hear the melody over the accompaniment, does it feel like the mood you asked
    for, and does it have a shape from beginning to end. The thinking stage (Thinking.h) uses it to
    choose between dozens of drafts. */
struct Judgement
{
    struct Aspect
    {
        std::string name;    // "Melody", "Harmony", ...
        double score = 0.0;  // 0-10
        double weight = 0.0; // its share of the total
        std::string note;    // the strongest or weakest point, in a few words
    };

    std::vector<Aspect> aspects;
    double total = 0.0;      // 0-100

    const Aspect* find (const std::string& name) const;
};

/** Judges a composed draft. `quality` and `request` must already be filled in (compose() does).
    `takeOverlap` (0-1) is how much of its melody repeats an earlier take, or -1 when there is none
    to compare with. */
Judgement judgeDraft (const Composition&, const StyleSpec&, double takeOverlap = -1.0);

} // namespace pm
