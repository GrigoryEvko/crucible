// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidTraceNumNames with the value
// SCHEMA_TABLE_CAP + 1 in constexpr context — the boundary edge
// fixture for the .crtrace optional schema-name table count cap.
//
// Per WRAP-TraceLoader-2 (#1050), ValidTraceNumNames is
// safety::Refined<safety::bounded_above<SCHEMA_TABLE_CAP>, uint32_t>
// with SCHEMA_TABLE_CAP == 512 (defined in SchemaTable.h).  The
// admissible range is [0, SCHEMA_TABLE_CAP] inclusive — empty name
// tables are well-formed; values above SCHEMA_TABLE_CAP would (without
// the gate) drive the name-registration loop past the SchemaTable's
// fixed-size entries[] array.
//
// Companion fixture: neg_trace_num_names_uint32_max.cpp
//   * This one is the boundary edge (= SCHEMA_TABLE_CAP + 1).
//   * That one is the wide miss (= UINT32_MAX).
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
    // (`bounded_above<SCHEMA_TABLE_CAP>(v)`) to be exercised at
    // compile time.  v == SCHEMA_TABLE_CAP + 1 → predicate(v) == false
    // → contract violation → not a constant expression → ill-formed.
    constexpr crucible::ValidTraceNumNames bad{
        uint32_t{crucible::SCHEMA_TABLE_CAP} + 1u};
    (void)bad;
    return 0;
}
