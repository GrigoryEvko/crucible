// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidTraceNumNames with the value
// UINT32_MAX in constexpr context — the wide-miss fixture for the
// .crtrace optional schema-name table count cap.
//
// Per WRAP-TraceLoader-2 (#1050), ValidTraceNumNames is
// safety::Refined<safety::bounded_above<SCHEMA_TABLE_CAP>, uint32_t>
// with SCHEMA_TABLE_CAP == 512.  UINT32_MAX is over 8 million times
// the cap.  Without the gate, the schema-name registration loop
// would attempt 4'294'967'295 fread+register iterations against a
// fixed-size entries[] array, silently truncating after 512 entries
// (the SchemaTable's silent-truncation policy) — but doing so AFTER
// reading 4 GB+ of disk bytes the file does not contain.
//
// Companion fixture: neg_trace_num_names_above_max.cpp
//   * That one is the boundary edge (= SCHEMA_TABLE_CAP + 1).
//   * This one is the upper-bound wide miss (= UINT32_MAX).  Catches
//     "drop the bound entirely" regression.
//
// In constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.

#include <crucible/TraceLoader.h>

#include <climits>
#include <cstdint>

int main() {
    // Constant evaluation forces the Refined ctor's pre clause
    // (`bounded_above<SCHEMA_TABLE_CAP>(v)`) to be exercised at
    // compile time.  v == UINT32_MAX → SCHEMA_TABLE_CAP < UINT32_MAX
    // → predicate(v) == false → contract violation → not a constant
    // expression → ill-formed.
    constexpr crucible::ValidTraceNumNames bad{uint32_t{UINT32_MAX}};
    (void)bad;
    return 0;
}
