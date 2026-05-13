// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidMetaAppendCount with the value
// UINT32_MAX in constexpr context — the wide-miss fixture for the
// MetaLog::try_append count-parameter cap.
//
// Per WRAP-MetaLog-2 (#945), ValidMetaAppendCount is
// safety::Refined<safety::bounded_above<MetaLog::CAPACITY>, uint32_t>
// with MetaLog::CAPACITY == (1u << 20) == 1'048'576.  UINT32_MAX is
// over 4'095× the cap.  Without the gate, downstream code that took
// UINT32_MAX as a count parameter would either spin forever in
// try_append's overflow check (graceful failure but mute) or, in any
// caller path that bypassed the check, drive a memcpy of
// 4'294'967'295 × 144 B ≈ 614 GB before SIGSEGV.
//
// Companion fixture: neg_metalog_append_count_above_max.cpp
//   * That one is the boundary edge (= MetaLog::CAPACITY + 1).
//   * This one is the upper-bound wide miss (= UINT32_MAX).  Catches
//     "drop the bound entirely" regression where ValidMetaAppendCount
//     degenerates from Refined<bounded_above<CAPACITY>> into a plain
//     uint32_t.
//
// In constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.

#include <crucible/MetaLog.h>

#include <climits>
#include <cstdint>

int main() {
    // Constant evaluation forces the Refined ctor's pre clause
    // (`bounded_above<MetaLog::CAPACITY>(v)`) to be exercised at
    // compile time.  v == UINT32_MAX → CAPACITY < UINT32_MAX →
    // predicate(v) == false → contract violation → not a constant
    // expression → ill-formed.
    constexpr crucible::ValidMetaAppendCount bad{uint32_t{UINT32_MAX}};
    (void)bad;
    return 0;
}
