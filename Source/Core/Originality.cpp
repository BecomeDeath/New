#include "Originality.h"
#include "OriginalityData.h"

#include <algorithm>
#include <map>
#include <unordered_set>

namespace pm
{

namespace
{
    constexpr int kIoiClasses[] = { 1, 2, 3, 4, 6, 8, 12, 16 };

    int ioiClass (int sixteenths)
    {
        int best = 0;

        for (int i = 0; i < 8; ++i)
            if (std::abs (kIoiClasses[i] - sixteenths) < std::abs (kIoiClasses[best] - sixteenths))
                best = i;

        return best;
    }

    bool isMelodic (const Track& t)
    {
        return ! t.isDrum && t.notes.size() >= 12 && (t.role == Role::Melody || t.role == Role::CounterMelody);
    }

    bool indexHas (uint64_t h, const void*) { return corpusHas (h); }

    /** Positions (run index within the part) of fingerprints the index knows, per phrase. */
    OriginalityResult scan (const Composition& comp, bool (*has) (uint64_t, const void*), const void* context)
    {
        OriginalityResult result;
        const int n = originality::ngram;

        for (auto& t : comp.tracks)
        {
            if (! isMelodic (t))
                continue;

            std::vector<int> hits;   // run positions across the whole part
            int offset = 0;

            for (auto& phrase : melodyMoves (t.notes))
            {
                for (int i = 0; i + n <= (int) phrase.size(); ++i)
                    if (has (movesHash (phrase.data() + i, n), context))
                        hits.push_back (offset + i);

                offset += (int) phrase.size() + n;   // never count across phrases
            }

            result.matches += (int) hits.size();

            // Several fingerprints close together = a passage that matches, not a coincidence.
            for (size_t a = 0; a < hits.size(); ++a)
            {
                size_t b = a;

                while (b + 1 < hits.size() && hits[b + 1] - hits[a] <= 24)
                    ++b;

                if (b - a + 1 >= 3)
                {
                    const int notes = hits[b] - hits[a] + n + 1;

                    if (notes > result.longestPassage)
                    {
                        result.longestPassage = notes;
                        result.track = t.name;
                    }

                    result.original = false;
                }
            }
        }

        return result;
    }

    std::vector<uint64_t> runHashes (const Composition& c, int n)
    {
        std::vector<uint64_t> out;

        for (auto& t : c.tracks)
            if (isMelodic (t))
                for (auto& phrase : melodyMoves (t.notes))
                    for (int i = 0; i + n <= (int) phrase.size(); ++i)
                        out.push_back (movesHash (phrase.data() + i, n));

        return out;
    }
}

std::vector<std::vector<int>> melodyMoves (const std::vector<Note>& notes)
{
    // The top line, one note per 16th (as the corpus lead lines are taken).
    std::map<int, int> sky;

    for (auto& note : notes)
    {
        const int q = (int) ((note.start + kPpq / 8) / (kPpq / 4));
        auto it = sky.find (q);

        if (it == sky.end() || note.pitch > it->second)
            sky[q] = note.pitch;
    }

    std::vector<std::vector<int>> phrases;
    std::vector<int> current;
    auto prev = sky.end();

    for (auto it = sky.begin(); it != sky.end(); ++it)
    {
        if (prev != sky.end())
        {
            const int ioi = it->first - prev->first;

            if (ioi <= 0 || ioi > 16)
            {
                if (! current.empty())
                    phrases.push_back (current);

                current.clear();
            }
            else
            {
                const int interval = std::max (-12, std::min (12, it->second - prev->second)) + 12;
                current.push_back (interval * 8 + ioiClass (ioi));
            }
        }

        prev = it;
    }

    if (! current.empty())
        phrases.push_back (current);

    return phrases;
}

uint64_t movesHash (const int* moves, int count)
{
    uint64_t h = 1469598103934665603ull;

    for (int i = 0; i < count; ++i)
        h = (h ^ (uint64_t) (moves[i] + 1)) * 1099511628211ull;

    h += 0x9E3779B97F4A7C15ull;
    h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ull;
    h = (h ^ (h >> 27)) * 0x94D049BB133111EBull;
    return h ^ (h >> 31);
}

bool corpusHas (uint64_t h)
{
    const uint64_t m = originality::bitCount;

    if (originality::fingerprints <= 0 || m == 0)
        return false;

    const uint32_t h1 = (uint32_t) h;
    const uint32_t h2 = (uint32_t) (h >> 32) | 1u;

    for (int i = 0; i < originality::hashes; ++i)
    {
        const uint64_t pos = (uint32_t) (h1 + (uint32_t) i * h2) % m;

        if (((originality::bits[pos >> 6] >> (pos & 63)) & 1ull) == 0)
            return false;
    }

    return true;
}

OriginalityResult checkOriginality (const Composition& comp)
{
    return scan (comp, &indexHas, nullptr);
}

OriginalityResult checkOriginality (const Composition& comp, bool (*has) (uint64_t, const void*), const void* context)
{
    return scan (comp, has, context);
}

double melodicOverlap (const Composition& a, const Composition& b)
{
    const int n = 6;
    const auto ha = runHashes (a, n);

    if (ha.empty())
        return 0.0;

    const auto hb = runHashes (b, n);
    const std::unordered_set<uint64_t> setB (hb.begin(), hb.end());
    int shared = 0;

    for (auto h : ha)
        shared += setB.count (h) > 0 ? 1 : 0;

    return (double) shared / (double) ha.size();
}

int corpusSize()
{
    return originality::pieces;
}

} // namespace pm
