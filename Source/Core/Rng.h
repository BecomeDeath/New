#pragma once

#include <cstdint>
#include <vector>

namespace pm
{

/** Small deterministic PRNG (splitmix64).

    std::uniform_*_distribution is implementation-defined, so the same seed would give
    different music on MSVC, libc++ and libstdc++. Rolling our own keeps a seed
    reproducible on every platform.
*/
class Rng
{
public:
    explicit Rng (uint64_t seed = 1) : state (seed ^ 0x9E3779B97F4A7C15ull) { next(); }

    uint64_t next()
    {
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    /** Uniform in [0, 1). */
    double uniform() { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }

    double uniform (double lo, double hi) { return lo + (hi - lo) * uniform(); }

    /** Uniform integer in [lo, hiInclusive]. */
    int range (int lo, int hiInclusive)
    {
        if (hiInclusive <= lo)
            return lo;

        return lo + (int) (next() % (uint64_t) (hiInclusive - lo + 1));
    }

    bool chance (double probability) { return uniform() < probability; }

    template <typename T>
    const T& pick (const std::vector<T>& items)
    {
        return items[(size_t) range (0, (int) items.size() - 1)];
    }

    /** Returns an index chosen with probability proportional to its weight. */
    int weighted (const std::vector<double>& weights)
    {
        double total = 0.0;

        for (auto w : weights)
            total += w > 0.0 ? w : 0.0;

        if (total <= 0.0)
            return 0;

        double r = uniform() * total;

        for (size_t i = 0; i < weights.size(); ++i)
        {
            const double w = weights[i] > 0.0 ? weights[i] : 0.0;

            if (r < w)
                return (int) i;

            r -= w;
        }

        return (int) weights.size() - 1;
    }

    /** Derives an independent generator, so adding a track doesn't reshuffle the others. */
    Rng fork (uint64_t salt) { return Rng (next() ^ (salt * 0xD1B54A32D192ED03ull)); }

private:
    uint64_t state;
};

} // namespace pm
