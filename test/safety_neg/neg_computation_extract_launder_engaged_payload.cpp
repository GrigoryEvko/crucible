// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-107 HS14 fixture #1.  Companion to
// neg_computation_extract_on_nonempty_row.cpp (which pins the
// trivial direct-engagement case).  This fixture pins the
// engagement-laundering scenario:
//
//     Computation<Row<>, Computation<Row<Effect::Bg>, int>>
//
// The OUTER row is empty so `row_size_v<R> == 0` holds — the
// vanilla METX-1 #473 constraint admits this.  But the PAYLOAD T
// is itself an ENGAGED Computation, and extract() returning that
// engaged Computation as a "pure" value would launder the inner
// Bg effect's audit trail through every operation seeing the
// outer's empty row.
//
// The FIXY-FOUND-107 gate `extract_admits_payload_v<T>` rejects
// this recursively: for T = Computation<R', U>, admission
// requires R' to be empty AND U to also admit.  Here R' =
// Row<Effect::Bg> is non-empty → trait rejects → constraint
// fails → compile error.
//
// Diagnostic carries "extract_admits_payload" (the trait name
// appears in the constraint failure message).

#include <crucible/effects/Computation.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/Capabilities.h>

using namespace crucible::effects;

int main() {
    using Inner = Computation<Row<Effect::Bg>, int>;
    using Outer = Computation<Row<>, Inner>;

    // Construct outer wrapping an engaged inner.
    auto inner = Computation<Row<>, int>::lift<Effect::Bg>(42);
    auto outer = Outer::mk(static_cast<Inner&&>(inner));

    // Should FAIL: outer's row is empty, but payload is engaged.
    // FIXY-FOUND-107 gate rejects.  Diagnostic mentions
    // "extract_admits_payload".
    auto leaked = outer.extract();
    return leaked.extract();
}
