// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-089 #2244 — HS14 mint-gate witness #2/2 for
// `safety::mint_simd_width_pinned<W, T, Args...>(args...)`.
//
// Violation: cardinality mismatch — T has no default ctor; zero args
// fails `is_constructible_v<NoDefaultCtor>`.
//
// Pairs with neg_mint_simd_width_pinned_unbuildable.cpp.
//
// Expected diagnostic: "constraints not satisfied" / "is_constructible"
// / "no matching function" / "no default constructor" / "deleted".

#include <crucible/safety/SimdWidthPinned.h>

using namespace ::crucible::safety;

struct NoDefaultCtor {
    int value;
    constexpr explicit NoDefaultCtor(int v) noexcept : value{v} {}
    NoDefaultCtor() = delete;
};

int main() {
    auto bad = mint_simd_width_pinned<SimdIsa_v::Scalar, NoDefaultCtor>();
    return bad.peek().value;
}
