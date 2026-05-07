// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidTraceNumOps with the value MAX_OPS + 1
// in constexpr context — the boundary edge fixture for the .crtrace
// header `num_ops` count cap.
//
// Per WRAP-TraceLoader-2 (#1050), ValidTraceNumOps is
// safety::Refined<safety::bounded_above<MAX_OPS>, uint32_t> with
// MAX_OPS == (1u << 22) == 4'194'304.  The admissible range is
// [0, MAX_OPS] inclusive — empty traces are well-formed; values above
// MAX_OPS would (without the gate) drive an std::vector<TraceOpRecord>
// allocation past 320 MB before truncation is discovered.
//
// Companion fixture: neg_trace_num_ops_uint32_max.cpp
//   * This one is the boundary edge (= MAX_OPS + 1).  Catches drift
//     where the bound widens from `bounded_above<MAX_OPS>` to
//     `bounded_above<MAX_OPS + K>`.
//   * That one is the wide miss (= UINT32_MAX).  Catches "drop the
//     bound entirely" regression where ValidTraceNumOps degenerates
//     to a plain `uint32_t` typedef; under that drift any 32-bit value
//     is silently accepted at construction, the only remaining defense
//     is the per-call-site `if (num_ops > MAX_OPS) return nullptr;`
//     comparison inside load_trace (which still detects the unmatched
//     value but only AFTER it has crossed the deserialize boundary —
//     losing the type-system gate that fires BEFORE the std::vector
//     allocation, and crucially, fires on aggregate-init paths that
//     might bypass load_trace entirely if a future reader of the same
//     file format is added without re-validating).
//
// In constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.

#include <crucible/TraceLoader.h>

#include <cstdint>

int main() {
    // Constant evaluation forces the Refined ctor's pre clause
    // (`bounded_above<MAX_OPS>(v)`) to be exercised at compile time.
    // v == MAX_OPS + 1 → predicate(v) == false → contract violation
    // → not a constant expression → ill-formed.
    constexpr crucible::ValidTraceNumOps bad{
        uint32_t{crucible::MAX_OPS} + 1u};
    (void)bad;
    return 0;
}
