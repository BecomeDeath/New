#pragma once

/** The originality index (Tools/corpus/build_originality_index.py, OriginalityData.cpp): a Bloom
    filter of distinctive melodic fingerprints from the reference corpus. No melody is stored. */
namespace pm::originality
{

extern const int ngram;                    // moves per fingerprint
extern const int window;                   // winnowing window used to pick them
extern const int hashes;                   // Bloom filter probes
extern const int pieces;                   // lead lines indexed
extern const int fingerprints;             // fingerprints in the filter
extern const unsigned long long bitCount;
extern const char* const digest;
extern const unsigned long long bits[];

} // namespace pm::originality
