// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-264 fixture #1 of 2 — distinct mismatch class:
// "a cache grant declared with an out-of-range prefetch locality".
//
// TraceRing's tracering_hw block declares grant::hw::cache<Prefetch, 3>.
// The cache grant template carries `requires (Locality >= 0 && Locality
// <= 3)` (FIXY-V-257) — the same domain as __builtin_prefetch's third
// argument.  This fixture instantiates the grant with locality 4, which
// violates that requires-clause, proving the NTTP range gate is
// load-bearing: a prefetch site that drifted to an undefined locality
// would red at the grant template-id rather than emit an undefined
// instruction.
//
// Distinct from neg_fixy_v_264_barrier_axis_mismatch.cpp: that exercises
// the BarrierStrength axis-routing of the fence grant; this exercises the
// HwInstruction cache grant's own NTTP-range requires-clause.
//
// Expected diagnostic substring: "constraints not satisfied" /
// "Locality" / "cache" / "associated constraints".

#include <crucible/fixy/Hw.h>

int main() {
    namespace fh  = ::crucible::fixy::hw;
    namespace fgh = ::crucible::fixy::grant::hw;
    // Should FAIL: locality 4 is outside the prefetch domain [0, 3], so the
    // cache<Prefetch, 4> template-id is ill-formed at instantiation.
    [[maybe_unused]] fgh::cache<fh::CacheOp::Prefetch, 4> bad{};
    return 0;
}
