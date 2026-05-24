// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-108 HS14 fixture #2.  Companion to
// neg_computation_then_callback_engaged_payload.cpp (single-level
// laundering on the callback return value).  This fixture pins the
// MULTI-LEVEL case:
//
//     k : T -> Computation<R2,
//                  Computation<R3_pure,
//                      Computation<R4_engaged, V>>>
//
// All visible rows down to the deepest payload are pure; only the
// innermost carries Bg.  The recursive `extract_admits_payload_v`
// trait walks every level and rejects at the deepest engaged row.
//
// Demonstrates the gate's recursion through nested-Computation
// callback returns — not just one level.

#include <crucible/effects/Computation.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/Capabilities.h>

using namespace crucible::effects;

int main() {
    using L3 = Computation<Row<Effect::Bg>, int>;   // engaged leaf
    using L2 = Computation<Row<>, L3>;              // pure wrapper
    using OuterValue = L2;                          // callback returns Comp<R2, L2>

    auto pure = Computation<Row<>, int>::mk(7);

    // Callback returns Computation<Row<>, L2> — outer row pure, L2
    // also pure, L3 engaged.  Recursion in extract_admits_payload
    // descends L2 → L3 → reject.
    auto chained = pure.then([](int) {
        auto leaf = Computation<Row<>, int>::lift<Effect::Bg>(42);
        auto mid  = L2::mk(static_cast<L3&&>(leaf));
        return Computation<Row<>, OuterValue>::mk(
            static_cast<OuterValue&&>(mid));
    });
    return chained.extract().extract().extract();
}
