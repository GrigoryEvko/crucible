// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidTraceNodeKindRaw with the value
// UINT8_MAX in a constexpr context — the wide-miss fixture for the
// TraceNodeKind bound.
//
// Per WRAP-Serialize-6 (#1015), ValidTraceNodeKindRaw is
// safety::Refined<safety::bounded_above<TraceNodeKind::TERMINAL>,
// uint8_t>.  0xFF is past every plausible TraceNodeKind enumerator
// value (TERMINAL == 3 currently) and would aliase to a kind that
// names no real DAG node category — every downstream branch would
// fail equality and fall through.
//
// In constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.
//
// Companion fixture: neg_trace_node_kind_above_terminal.cpp
//   * That one is the boundary edge (= TERMINAL + 1, off-by-one).
//   * This one is the wide miss (= UINT8_MAX).  Catches "drop the
//     bound entirely" regression where ValidTraceNodeKindRaw
//     degenerates from `Refined<bounded_above<TERMINAL>, uint8_t>`
//     into a plain `uint8_t` typedef; under that drift any byte is
//     silently accepted at construction and the only remaining
//     defense is the per-call-site `kind == X` comparisons inside
//     recompute_merkle / iterate / walk_and_recompute_merkle (which
//     still detect the unmatched value but only AFTER it has crossed
//     the read_header boundary — losing the type-system gate that
//     fires BEFORE any DAG traversal call ever lands, and crucially,
//     fires on aggregate-init / memcpy / aliasing paths that bypass
//     read_header entirely).
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.

#include <crucible/ir001/MerkleDag.h>

#include <climits>
#include <cstdint>

int main() {
    // Constant evaluation forces the Refined ctor's pre clause
    // (`bounded_above<TERMINAL>(v)`) to be exercised at compile time.
    // v == UINT8_MAX → TERMINAL < UINT8_MAX → predicate(v) == false
    // → contract violation → not a constant expression → ill-formed.
    constexpr crucible::ValidTraceNodeKindRaw bad{
        uint8_t{UINT8_MAX}};
    (void)bad;
    return 0;
}
