// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-089 #2244 — HS14 mint-gate witness #1/2 for
// `safety::mint_barrier_guarded<Tier, T, Args...>(args...)`.
//
// Violation: calling the mint with args that cannot construct T.
// The requires-clause `std::is_constructible_v<T, Args...>` MUST
// reject the call at the §XXI mint boundary.
//
// HS14 floor per CLAUDE.md §XXI: every mint factory ships ≥2 neg
// fixtures.  Distinct mismatch classes for this requires-clause:
//   #1 (this) — type mismatch:   OnlyIntCtor + const char* arg fails
//                                is_constructible_v on argument type
//                                conversion (no ctor accepts pointer).
//   #2 (peer) — cardinality:     NoDefaultCtor + zero args fails
//                                is_constructible_v on count (ctor
//                                requires an int; none supplied).
//
// Both fixtures fire the SAME requires-clause but for distinct reasons,
// matching §XXI's "≥2 distinct mismatch class" rubric.  A refactor that
// loosened the gate (dropped requires, replaced with `true`) would
// silently accept either path and defer the diagnostic to a deep
// in-template SFINAE site — these fixtures pin the rejection AT the
// mint surface.
//
// Expected diagnostic: "constraints not satisfied" / "is_constructible"
// / "no matching function" / "cannot convert".

#include <crucible/safety/BarrierGuarded.h>

using namespace ::crucible::safety;

struct OnlyIntCtor {
    int value;
    constexpr explicit OnlyIntCtor(int v) noexcept : value{v} {}
};

int main() {
    auto bad = mint_barrier_guarded<BarrierStrength_v::AcqRel, OnlyIntCtor>(
        "not_an_integer");
    return bad.peek().value;
}
