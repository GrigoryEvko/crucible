// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-089 #2244 — HS14 mint-gate witness #1/2 for
// `safety::mint_simd_width_pinned<W, T, Args...>(args...)`.
//
// Violation: type mismatch on the requires-clause
// `std::is_constructible_v<T, Args...>`.  SimdWidthPinned wraps a
// payload under a pinned ISA grade; the mint surface still gates
// constructibility at §XXI's mint boundary.
//
// Pairs with neg_mint_simd_width_pinned_no_default_ctor.cpp.
//
// Expected diagnostic: "constraints not satisfied" / "is_constructible"
// / "no matching function" / "cannot convert".

#include <crucible/safety/SimdWidthPinned.h>

using namespace ::crucible::safety;

struct OnlyIntCtor {
    int value;
    constexpr explicit OnlyIntCtor(int v) noexcept : value{v} {}
};

int main() {
    auto bad = mint_simd_width_pinned<SimdIsa_v::Scalar, OnlyIntCtor>(
        "not_an_integer");
    return bad.peek().value;
}
