// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-089 #2244 — HS14 mint-gate witness #1/2 for
// `safety::mint_fp_mode_composite<R, F, C, Tr, D, N, I, Cl, L, Re,
//  Cr, T, Args...>(args...)`.
//
// Violation: type mismatch on the requires-clause
// `std::is_constructible_v<T, Args...>`.  The composite-mint pins
// all 11 FP axes (rounding/ftz/contract/trap/denormal/nan/inf/
// complex/libm/reassoc/const-round); the requires-clause gates the
// payload-ctor exactly as the per-axis mints do.
//
// Pairs with neg_mint_fp_mode_composite_no_default_ctor.cpp.
//
// Expected diagnostic: "constraints not satisfied" / "is_constructible"
// / "no matching function" / "cannot convert".

#include <crucible/safety/FpMode.h>

namespace cs = ::crucible::safety;

struct OnlyIntCtor {
    int value;
    constexpr explicit OnlyIntCtor(int v) noexcept : value{v} {}
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
        OnlyIntCtor>("not_an_integer");
    return bad.peek().value;
}
