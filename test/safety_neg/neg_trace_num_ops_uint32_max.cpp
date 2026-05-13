// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidTraceNumOps with the value UINT32_MAX
// in constexpr context — the wide-miss fixture for the .crtrace
// header `num_ops` count cap.
//
// Per WRAP-TraceLoader-2 (#1050), ValidTraceNumOps is
// safety::Refined<safety::bounded_above<MAX_OPS>, uint32_t> with
// MAX_OPS == (1u << 22) == 4'194'304.  UINT32_MAX is over a thousand
// times the cap — a wire value that adversarial files inject to
// trigger pre-truncation allocation overflow.  Without the gate,
// `std::vector<TraceOpRecord> records(UINT32_MAX)` would attempt to
// allocate 4'294'967'295 × 80 B ≈ 320 GB before fread() discovers
// truncation; with the gate, the contract violation halts the load
// before any allocation.
//
// Companion fixture: neg_trace_num_ops_above_max.cpp
//   * That one is the boundary edge (= MAX_OPS + 1).
//   * This one is the upper-bound wide miss (= UINT32_MAX).  Catches
//     "drop the bound entirely" regression where ValidTraceNumOps
//     degenerates to a plain `uint32_t` typedef.
//
// In constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.

#include <crucible/ir001/TraceLoader.h>

#include <climits>
#include <cstdint>

int main() {
    // Constant evaluation forces the Refined ctor's pre clause
    // (`bounded_above<MAX_OPS>(v)`) to be exercised at compile time.
    // v == UINT32_MAX → MAX_OPS < UINT32_MAX → predicate(v) == false
    // → contract violation → not a constant expression → ill-formed.
    constexpr crucible::ValidTraceNumOps bad{uint32_t{UINT32_MAX}};
    (void)bad;
    return 0;
}
