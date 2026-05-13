// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidMetaAppendCount with the value
// MetaLog::CAPACITY + 1 in constexpr context — the boundary edge
// fixture for the MetaLog::try_append count-parameter cap.
//
// Per WRAP-MetaLog-2 (#945), ValidMetaAppendCount is
// safety::Refined<safety::bounded_above<MetaLog::CAPACITY>, uint32_t>
// with MetaLog::CAPACITY == (1u << 20) == 1'048'576.  The admissible
// range is [0, CAPACITY] inclusive — empty appends are well-formed,
// and a single batch may legitimately span the entire buffer; values
// above CAPACITY would (without the gate) drive try_append's runtime
// overflow check to fail forever, leaving the producer wedged at
// MetaIndex::none() with no diagnostic surface.
//
// Companion fixture: neg_metalog_append_count_uint32_max.cpp
//   * This one is the boundary edge (= CAPACITY + 1).  Catches drift
//     where the bound widens from
//     `bounded_above<MetaLog::CAPACITY>` to
//     `bounded_above<MetaLog::CAPACITY + K>` for any K >= 1.
//   * That one is the upper-bound wide miss (= UINT32_MAX).  Catches
//     "drop the bound entirely" regression.
//
// In constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.

#include <crucible/ir001/MetaLog.h>

#include <cstdint>

int main() {
    // Constant evaluation forces the Refined ctor's pre clause
    // (`bounded_above<MetaLog::CAPACITY>(v)`) to be exercised at
    // compile time.  v == CAPACITY + 1 → predicate(v) == false →
    // contract violation → not a constant expression → ill-formed.
    constexpr crucible::ValidMetaAppendCount bad{
        uint32_t{crucible::MetaLog::CAPACITY} + 1u};
    (void)bad;
    return 0;
}
