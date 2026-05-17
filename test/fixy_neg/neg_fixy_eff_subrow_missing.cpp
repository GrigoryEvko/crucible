// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-EFF fixture #1: Subrow concept rejects when caller's row
// excludes the callee's atom, routed through `fixy::eff::Subrow`.
//
// Violation: `Row<Effect::Bg>` is NOT a subrow of `Row<>` — the empty
// foreground row cannot satisfy a function that demands Bg capability.
// Subrow<Row<Effect::Bg>, Row<>>` is false; static-asserting it via
// fixy::eff::Subrow must reject.
//
// Expected diagnostic: GCC's static_assert text pointing at the
// "fixy::eff::Subrow<Row<Bg>, Row<>>" claim.

#include <crucible/fixy/Eff.h>

namespace fe = crucible::fixy::eff;

// Unique-carrier discipline (HS14 anti-collision).
struct EffNegFixture1_Marker {};

int main() {
    // Hot foreground row Row<> does NOT contain Effect::Bg, so the
    // Subrow direction Row<Bg> ⊆ Row<> is false.  fixy alias passes
    // the substrate's concept rejection through unchanged.
    static_assert(fe::Subrow<fe::Row<fe::Effect::Bg>, fe::Row<>>,
        "fixy::eff::Subrow<Row<Bg>, Row<>> must reject — foreground "
        "row Row<> cannot satisfy a Bg-required call.  Alias preserves "
        "the substitution-principle gate.");
    (void)sizeof(EffNegFixture1_Marker);
    return 0;
}
