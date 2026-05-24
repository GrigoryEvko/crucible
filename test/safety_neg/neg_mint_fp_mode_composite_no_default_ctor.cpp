// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-089 #2244 — HS14 mint-gate witness #2/2 for
// `safety::mint_fp_mode_composite<...>(args...)`.
//
// Violation: cardinality mismatch — T has no default ctor; zero
// args fails `is_constructible_v<NoDefaultCtor>`.  The composite
// 11-deep nest delegates to the innermost FpConstantRoundingPinned
// layer, which forwards args to T via std::in_place — the requires-
// clause is the only gate before that delegation.
//
// Pairs with neg_mint_fp_mode_composite_unbuildable.cpp.
//
// Expected diagnostic: "constraints not satisfied" / "is_constructible"
// / "no matching function" / "no default constructor" / "deleted".

#include <crucible/safety/FpMode.h>

namespace cs = ::crucible::safety;

struct NoDefaultCtor {
    int value;
    constexpr explicit NoDefaultCtor(int v) noexcept : value{v} {}
    NoDefaultCtor() = delete;
};

int main() {
    auto bad = cs::mint_fp_mode_composite<
        cs::FpRounding::RoundToNearestEven,
        cs::FpFtz::FlushToZero,
        cs::FpContract::Off,
        cs::FpTrapMask::AllMasked,
        cs::FpDenormalInput::HonorDenormals,
        cs::FpNanPolicy::PropagateQuiet,
        cs::FpInfPolicy::PropagateInfinity,
        cs::FpComplexLayout::Interleaved,
        cs::FpLibmPolicy::ScalarLibm,
        cs::FpReassociate::Forbidden,
        cs::FpConstantRounding::SameAsRuntime,
        NoDefaultCtor>();
    return bad.peek().value;
}
