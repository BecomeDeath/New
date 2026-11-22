#pragma once

#include "Composition.h"
#include "Request.h"

namespace pm
{

/** Makes a plan meet the request before anything is composed: the asked-for key, scale, tempo,
    meter and length; instruments left out or added; an ensemble trimmed to its size ("trio");
    the lead given to the instrument that was asked to lead. Works on any plan, the offline
    interpreter's or Claude's. */
void applyRequest (StyleSpec&, const Request&);

/** Checks the finished notes against the request, criterion by criterion. */
RequestReport checkRequest (const Composition&, const Request&);

/** Does a track count as this instrument? (programs, then names: "violin" matches "Violins I") */
bool trackIs (const std::string& trackName, int program, bool percussion, const InstrumentNeed&);

/** The instrument a track belongs to, for counting an ensemble: "Piano (Melody)" and "Piano
    (Accompaniment)" are one piano, "Violin I" and "Violin II" are two players. */
std::string instrumentKey (const std::string& trackName, int program, bool percussion);

} // namespace pm
