#pragma once

#include "Composition.h"

#include <cstdint>

namespace pm
{

/** Turns a StyleSpec into notes.

    The result is fully deterministic for a given (spec, seed) pair on every platform,
    so "Variation" just means composing the same spec with another seed.
*/
Composition compose (const StyleSpec& spec, uint64_t seed);

} // namespace pm
