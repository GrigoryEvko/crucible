// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-089 #2244 — HS14 mint-gate witness #1/2 for
// `safety::mint_hw<Tier, T, Args...>(args...)`.
//
// Violation: type-mismatch on the requires-clause
// `std::is_constructible_v<T, Args...>`.  Mirrors the §XXI mint
// rubric for the HwInstruction-grade family.
//
// Pairs with neg_mint_hw_no_default_ctor.cpp (cardinality mismatch)
// for the 2-fixture HS14 floor.
//
// Expected diagnostic: "constraints not satisfied" / "is_constructible"
// / "no matching function" / "cannot convert".

#include <crucible/safety/Hw.h>

using namespace ::crucible::safety;

struct OnlyIntCtor {
    int value;
    constexpr explicit OnlyIntCtor(int v) noexcept : value{v} {}
};

int main() {
    auto bad = mint_hw<HwInstruction_v::Scalar, OnlyIntCtor>("not_an_integer");
    return bad.peek().value;
}
