// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidDrainCount with the value
// TraceRing::CAPACITY + 1 in constexpr context — the boundary edge
// fixture for the TraceRing drain max-count cap.
//
// Per WRAP-TraceRing-3 (#1055), ValidDrainCount is
// safety::Refined<safety::bounded_above<TraceRing::CAPACITY>, uint32_t>
// with TraceRing::CAPACITY == (1u << 16) == 65'536.  Values above
// CAPACITY are meaningless (the consumer never has more than CAPACITY
// entries available, so the inner std::min(available, max_count)
// silently clamps), but the silent clamp masks call-site arithmetic
// errors that the type-level gate now catches at construction.
//
// Companion fixture: neg_trace_ring_drain_count_uint32_max.cpp
//   * This one is the boundary edge (= CAPACITY + 1).  Catches drift
//     where the bound widens from
//     `bounded_above<TraceRing::CAPACITY>` to
//     `bounded_above<TraceRing::CAPACITY + K>` for any K >= 1.
//   * That one is the upper-bound wide miss (= UINT32_MAX).  Catches
//     "drop the bound entirely" regression.
//
// In constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.

#include <crucible/TraceRing.h>

#include <cstdint>

int main() {
    // Constant evaluation forces the Refined ctor's pre clause
    // (`bounded_above<TraceRing::CAPACITY>(v)`) to be exercised at
    // compile time.  v == CAPACITY + 1 → predicate(v) == false →
    // contract violation → not a constant expression → ill-formed.
    constexpr crucible::ValidDrainCount bad{
        uint32_t{crucible::TraceRing::CAPACITY} + 1u};
    (void)bad;
    return 0;
}
