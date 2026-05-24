// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-107 HS14 fixture #2.  Companion to
// neg_computation_extract_launder_engaged_payload.cpp (single-
// level laundering).  This fixture pins the multi-level case:
//
//     Computation<Row<>,
//         Computation<Row<>,
//             Computation<Row<Effect::Bg>, int>>>
//
// All outer rows are empty; only the deepest payload carries an
// engaged Row<Bg>.  The FIXY-FOUND-107 gate
// `extract_admits_payload_v<T>` recursively descends through the
// pure outer layers and rejects when the engaged inner is found.
//
// This proves the recursion in the trait specialization
//     extract_admits_payload<Computation<R, U>> :=
//         (row_size_v<R> == 0) && extract_admits_payload<U>
// actually walks the nesting — not just one layer.
//
// Diagnostic carries "extract_admits_payload" — the recursive
// constraint fails at the deepest level, GCC reports the trait.

#include <crucible/effects/Computation.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/Capabilities.h>

using namespace crucible::effects;

int main() {
    using L3 = Computation<Row<Effect::Bg>, int>;
    using L2 = Computation<Row<>, L3>;
    using L1 = Computation<Row<>, L2>;

    auto leaf = Computation<Row<>, int>::lift<Effect::Bg>(42);
    auto mid  = L2::mk(static_cast<L3&&>(leaf));
    auto top  = L1::mk(static_cast<L2&&>(mid));

    // Should FAIL: outer two rows are empty but deepest payload is
    // Row<Bg>-engaged.  Recursive gate descends, rejects at L3.
    auto leaked = top.extract();
    return leaked.extract().extract();
}
