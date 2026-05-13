// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidTraceNumMetas with the value
// MAX_METAS + 1 in constexpr context — the boundary edge fixture for
// the .crtrace header `num_metas` count cap.
//
// Per WRAP-TraceLoader-2 (#1050), ValidTraceNumMetas is
// safety::Refined<safety::bounded_above<MAX_METAS>, uint32_t> with
// MAX_METAS == (1u << 24) == 16'777'216.  The admissible range is
// [0, MAX_METAS] inclusive — empty traces are well-formed; values
// above MAX_METAS would (without the gate) drive an
// std::vector<TensorMeta> allocation past 2.7 GB before truncation
// is discovered.
//
// Companion fixture: neg_trace_num_metas_uint32_max.cpp
//   * This one is the boundary edge (= MAX_METAS + 1).  Catches drift
//     where the bound widens from `bounded_above<MAX_METAS>` to
//     `bounded_above<MAX_METAS + K>`.
//   * That one is the wide miss (= UINT32_MAX).  Catches "drop the
//     bound entirely" regression.
//
// In constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.

#include <crucible/ir001/TraceLoader.h>

#include <cstdint>

int main() {
    // Constant evaluation forces the Refined ctor's pre clause
    // (`bounded_above<MAX_METAS>(v)`) to be exercised at compile time.
    // v == MAX_METAS + 1 → predicate(v) == false → contract violation
    // → not a constant expression → ill-formed.
    constexpr crucible::ValidTraceNumMetas bad{
        uint32_t{crucible::MAX_METAS} + 1u};
    (void)bad;
    return 0;
}
