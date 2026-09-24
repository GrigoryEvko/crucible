// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidSchemaNameLen with the value
// UINT16_MAX in constexpr context — the wide-miss fixture for the
// schema-name-length bound.
//
// Per WRAP-TraceLoader-3 (#1051), ValidSchemaNameLen is
// safety::Refined<safety::in_range<SCHEMA_NAME_LEN_MIN,
// SCHEMA_NAME_LEN_MAX>, uint16_t> with MIN == 1 and MAX == 256.  The
// admissible range is the closed interval [1, 256].  UINT16_MAX is
// far past 256 — the .crtrace loader's stack name buffer is exactly
// 257 bytes (256 chars + a null terminator), so a name_len of 65535
// would (without the gate) drive both the fread and the trailing
// `name_buf[name_len] = '\0'` write past the buffer — heap-buffer-
// overflow under ASan, silent stack corruption otherwise.
//
// In constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.
//
// Companion fixture: neg_schema_name_len_below_min.cpp
//   * That one is the lower-bound edge (= 0).  Catches drift where
//     the lower bound widens from `in_range<1, 256>` to
//     `in_range<0, 256>`, silently admitting a zero-length name.
//   * This one is the upper-bound wide miss (= UINT16_MAX).  Catches
//     "drop the upper bound entirely" regression where
//     ValidSchemaNameLen degenerates from
//     `Refined<in_range<1, 256>, uint16_t>` into a plain
//     `uint16_t` typedef; under that drift any 16-bit value is
//     silently accepted at construction, the only remaining defense
//     is the per-call-site `if (raw_name_len > SCHEMA_NAME_LEN_MAX) break;`
//     comparison inside load_trace (which still detects the unmatched
//     value but only AFTER it has crossed the deserialize boundary —
//     losing the type-system gate that fires BEFORE any fread call
//     ever lands, and crucially, fires on aggregate-init paths that
//     might bypass load_trace entirely if a future reader of the same
//     file format is added without re-validating).
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.

#include <crucible/TraceLoader.h>

#include <climits>
#include <cstdint>

int main() {
    // Constant evaluation forces the Refined ctor's pre clause
    // (`in_range<1, 256>(v)`) to be exercised at compile time.
    // v == UINT16_MAX → 256 < UINT16_MAX → predicate(v) == false
    // → contract violation → not a constant expression → ill-formed.
    constexpr crucible::ValidSchemaNameLen bad{uint16_t{UINT16_MAX}};
    (void)bad;
    return 0;
}
