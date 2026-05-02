// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for effects::EffectRowProjection (#1087).
//
// Premise: row_subsumes_bits<R>(sample) where R is NOT a Row<Es...>
// instantiation MUST fail at the IsEffectRow concept gate.  Same
// rationale as the bits_from_row neg-compile fixture — a non-Row
// argument cannot be meaningfully checked against a runtime Bits
// sample, so the concept gate rejects up front rather than letting
// the template substitution dive into nonsense diagnostics.
//
// Expected diagnostic: "associated constraints are not satisfied" /
// "constraints not satisfied" / "no matching function" pointing at
// the row_subsumes_bits<int>(...) call site.

#include <crucible/effects/EffectRowProjection.h>
#include <crucible/safety/Bits.h>

namespace eff = crucible::effects;
namespace saf = crucible::safety;

int main() {
    saf::Bits<eff::Effect> sample{};

    // Bridge fires: row_subsumes_bits<R>() requires IsEffectRow<R>;
    // R = int does not match the Row<Es...> partial spec.  The
    // concept gate rejects regardless of the runtime sample value.
    auto bad = eff::row_subsumes_bits<int>(sample);
    (void)bad;
    return 0;
}
