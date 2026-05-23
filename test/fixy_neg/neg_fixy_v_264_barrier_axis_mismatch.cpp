// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-264 fixture #2 of 2 — distinct mismatch class:
// "a fence grant mis-asserted as the wrong dimension axis".
//
// ChaseLevDeque's chaselev_hw block declares barrier_compiler_seqcst and
// asserts which_dim_v<...> == BarrierStrength.  This fixture replicates
// the routing check with the WRONG axis (SimdIsa — where V-262/V-263's
// simd::width grants live), proving the barrier grant routes to a DISTINCT
// axis from the SIMD-width grants: a copy-paste that asserted the fence on
// the SimdIsa axis would red here.
//
// Distinct from neg_fixy_v_264_cache_locality_overflow.cpp: that exercises
// the cache grant's NTTP-range requires-clause; this exercises the barrier
// grant's BarrierStrength axis routing.
//
// Expected diagnostic substring: FIXY-V-264 / "static assertion" /
// "SimdIsa" / "BarrierStrength".

#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Hw.h>

int main() {
    namespace fh = ::crucible::fixy::hw;
    namespace fg = ::crucible::fixy::grant;
    using D = ::crucible::fixy::dim::DimensionAxis;
    // Should FAIL: barrier_compiler_seqcst routes to BarrierStrength, NOT
    // SimdIsa — the fence grant is on a different axis than the V-262/263
    // simd::width grants.
    static_assert(fg::which_dim_v<fh::barrier_compiler_seqcst> == D::SimdIsa,
        "FIXY-V-264 neg: a barrier grant must NOT route to SimdIsa — it is "
        "BarrierStrength.  This assertion is deliberately wrong.");
    return 0;
}
