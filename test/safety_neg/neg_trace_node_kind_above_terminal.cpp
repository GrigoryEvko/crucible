// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidTraceNodeKindRaw with the value
// TERMINAL + 1 (currently 4) in a constexpr context — fires
// Refined<bounded_above<TERMINAL>, uint8_t>'s pre-clause exactly at
// the boundary edge.
//
// Per WRAP-Serialize-6 (#1015), ValidTraceNodeKindRaw is
// safety::Refined<safety::bounded_above<TERMINAL>, uint8_t>, the typed
// gate at every uint8_t → TraceNodeKind widening site (Cipher
// deserialize via Serialize::read_header, future TraceLoader paths,
// FFI bridges).  The bound admits 0 (REGION) through
// static_cast<uint8_t>(TERMINAL) (currently 3) and rejects every byte
// past TERMINAL — those are the bytes that would propagate as an
// invalid TraceNodeKind enumerator into the exhaustive switch at
// recompute_merkle and the kind-comparison loops in the iterate /
// walk_and_recompute_merkle path.
//
// In constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.
//
// Companion fixture: neg_trace_node_kind_uint8_max.cpp
//   * This one is the boundary edge (= TERMINAL + 1, off-by-one).
//     Catches drift where the bound widens from `TERMINAL` to
//     `TERMINAL + K` for some K >= 1, silently admitting a byte that
//     names no valid TraceNodeKind enumerator.  The exhaustive switch
//     at recompute_merkle has no default arm — a kind value past
//     TERMINAL falls through into UB territory under
//     `-Wswitch -Werror` (the value is *not* one of REGION / BRANCH /
//     LOOP / TERMINAL, so every case-label compares unequal and the
//     switch returns without setting a result).
//   * That one is the wide miss (= UINT8_MAX).  Catches "drop the
//     bound entirely" regression where ValidTraceNodeKindRaw
//     degenerates into a plain uint8_t alias and admits any byte.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.

#include <crucible/ir001/MerkleDag.h>

#include <cstdint>

int main() {
    // Constant evaluation forces the Refined ctor's pre clause
    // (`bounded_above<TERMINAL>(v)`) to be exercised at compile time.
    // v == TERMINAL + 1 → predicate(v) == false → contract violation
    // → the expression is not a constant expression → ill-formed
    // initialization of constexpr local.
    constexpr crucible::ValidTraceNodeKindRaw bad{
        static_cast<uint8_t>(
            static_cast<uint8_t>(crucible::TraceNodeKind::TERMINAL)
            + uint8_t{1})};
    (void)bad;
    return 0;
}
