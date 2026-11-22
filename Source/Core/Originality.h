#pragma once

#include "Composition.h"

#include <cstdint>
#include <string>
#include <vector>

namespace pm
{

/** Melodic fingerprints: a tune as "moves" - the interval to the next note (-12..12 semitones)
    and the time to it (a 16th-note class) - so a passage is recognised in any key or tempo.
    The same definition as Tools/corpus/build_originality_index.py. */
std::vector<std::vector<int>> melodyMoves (const std::vector<Note>& notes);
uint64_t movesHash (const int* moves, int count);

/** Is this fingerprint in the reference corpus index? (A Bloom filter: never a false "no".) */
bool corpusHas (uint64_t fingerprint);

struct OriginalityResult
{
    bool original = true;
    int matches = 0;            // fingerprints found in the corpus (isolated ones are coincidences)
    int longestPassage = 0;     // notes in the longest run that looks copied
    std::string track;          // where
};

/** Checks every melodic part of a piece against the corpus: a passage counts as copied when
    several distinctive fingerprints of one part are found close together (about 16 notes or
    more of a corpus melody, in any key). */
OriginalityResult checkOriginality (const Composition&);

/** The same test against fingerprints you supply (tests, and comparing takes). */
OriginalityResult checkOriginality (const Composition&, bool (*has) (uint64_t, const void*), const void* context);

/** How much of a's melodies b shares: the fraction of a's melodic runs that also occur in b (0-1). */
double melodicOverlap (const Composition& a, const Composition& b);

/** How many lead lines the index was built from. */
int corpusSize();

} // namespace pm
