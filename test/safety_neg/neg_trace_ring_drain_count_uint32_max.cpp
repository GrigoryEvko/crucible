// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidDrainCount with the value
// UINT32_MAX in constexpr context — the wide-miss fixture for the
// TraceRing drain max-count cap.
//
// Per WRAP-TraceRing-3 (#1055), ValidDrainCount is
// safety::Refined<safety::bounded_above<TraceRing::CAPACITY>, uint32_t>
// with TraceRing::CAPACITY == (1u << 16) == 65'536.  UINT32_MAX is
// 65 537× the cap.  Without the gate, a caller that loaded a uint32_t
// max_count from disk / env / FFI without bounds-checking would
// silently slip the runtime overflow at the inner std::min, masking
// the real call-site arithmetic bug behind a silent clamp.
//
// Companion fixture: neg_trace_ring_drain_count_above_max.cpp
//   * That one is the boundary edge (= TraceRing::CAPACITY + 1).
//   * This one is the upper-bound wide miss (= UINT32_MAX).  Catches
//     "drop the bound entirely" regression where ValidDrainCount
//     degenerates from Refined<bounded_above<CAPACITY>> into a plain
//     uint32_t.
//
// In constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.

#include <crucible/TraceRing.h>

#include <climits>
#include <cstdint>

int main() {
    // Constant evaluation forces the Refined ctor's pre clause
    // (`bounded_above<TraceRing::CAPACITY>(v)`) to be exercised at
    // compile time.  v == UINT32_MAX → CAPACITY < UINT32_MAX →
    // predicate(v) == false → contract violation → not a constant
    // expression → ill-formed.
    constexpr crucible::ValidDrainCount bad{uint32_t{UINT32_MAX}};
    (void)bad;
    return 0;
}
