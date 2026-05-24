// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-108 HS14 fixture #1.  Symmetric to
// neg_computation_extract_launder_engaged_payload.cpp: that fixture
// closes the laundering shape on the EXTRACT side; this one closes
// it on the THEN-callback side.
//
// then(k) accumulates R ∪ R2 from the callback's declared row R2.
// If the callback returns
//     Computation<R2, Computation<R3, V>>
// where R3 is engaged, the inner R3 row is invisible in the chain's
// result type — every downstream operation sees Computation<R∪R2,
// Computation<R3, V>> and the inner row's audit trail is laundered
// past the union.
//
// FIXY-FOUND-108 reuses the FIXY-FOUND-107 `extract_admits_payload_v
// <U>` trait on the callback's invoke_result_t::value_type to reject
// such structures.
//
// Diagnostic carries "extract_admits_payload" — the trait name
// appears in the constraint-failure message.

#include <crucible/effects/Computation.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/Capabilities.h>

using namespace crucible::effects;

int main() {
    using EngagedInner = Computation<Row<Effect::Bg>, int>;

    auto pure = Computation<Row<>, int>::mk(7);

    // Callback returns Computation<Row<>, EngagedInner> — the OUTER
    // row claims pure but the VALUE is a Bg-engaged Computation.
    // Should FAIL: extract_admits_payload_v<EngagedInner> is false.
    auto chained = pure.then([](int) {
        return Computation<Row<>, EngagedInner>::mk(
            Computation<Row<>, int>::lift<Effect::Bg>(42));
    });
    return chained.extract().extract();
}
