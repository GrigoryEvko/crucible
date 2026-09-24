// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-089 #2244 — HS14 mint-gate witness #2/2 for
// `safety::mint_barrier_guarded<Tier, T, Args...>(args...)`.
//
// Violation: zero args supplied for a T that has no default ctor.
// The requires-clause `std::is_constructible_v<T, Args...>` MUST
// reject the call at the §XXI mint boundary (cardinality mismatch:
// `is_constructible_v<NoDefaultCtor>` is false).
//
// Pairs with neg_mint_barrier_guarded_unbuildable.cpp — type vs.
// cardinality mismatch are §XXI's two distinct mismatch classes.
//
// Expected diagnostic: "constraints not satisfied" / "is_constructible"
// / "no matching function" / "no default constructor" / "no
// matching constructor for initialization".

#include <crucible/safety/BarrierGuarded.h>

using namespace ::crucible::safety;

struct NoDefaultCtor {
    int value;
    constexpr explicit NoDefaultCtor(int v) noexcept : value{v} {}
    NoDefaultCtor() = delete;
};

int main() {
    auto bad = mint_barrier_guarded<BarrierStrength_v::AcqRel, NoDefaultCtor>();
    return bad.peek().value;
}
