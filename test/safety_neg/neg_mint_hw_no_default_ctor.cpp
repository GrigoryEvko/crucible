// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-089 #2244 — HS14 mint-gate witness #2/2 for
// `safety::mint_hw<Tier, T, Args...>(args...)`.
//
// Violation: cardinality mismatch on the requires-clause — T has no
// default ctor; zero args supplied makes
// `is_constructible_v<NoDefaultCtor>` false.
//
// Pairs with neg_mint_hw_unbuildable.cpp (type mismatch).
//
// Expected diagnostic: "constraints not satisfied" / "is_constructible"
// / "no matching function" / "no default constructor" / "deleted".

#include <crucible/safety/Hw.h>

using namespace ::crucible::safety;

struct NoDefaultCtor {
    int value;
    constexpr explicit NoDefaultCtor(int v) noexcept : value{v} {}
    NoDefaultCtor() = delete;
};

int main() {
    auto bad = mint_hw<HwInstruction_v::Scalar, NoDefaultCtor>();
    return bad.peek().value;
}
