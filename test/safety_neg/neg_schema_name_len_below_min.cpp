// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidSchemaNameLen with the value 0 in
// constexpr context — the lower-bound edge fixture for the schema-
// name-length bound.
//
// Per WRAP-TraceLoader-3 (#1051), ValidSchemaNameLen is
// safety::Refined<safety::in_range<SCHEMA_NAME_LEN_MIN,
// SCHEMA_NAME_LEN_MAX>, uint16_t> with MIN == 1 and MAX == 256.  The
// admissible range is the closed interval [1, 256].  A name_len of 0
// is the canonical drift fixture — the .crtrace format pairs each
// schema_hash with at least one byte of name; admitting 0 would
// silently truncate the loop with a zero-byte name (no fread on the
// name buffer), and the trailing `name_buf[0] = '\0'` would associate
// a SchemaHash with the empty string in the global SchemaTable —
// every subsequent lookup() would return the empty entry instead of
// the real one.
//
// In constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.
//
// Companion fixture: neg_schema_name_len_above_max.cpp
//   * This one is the lower-bound edge (= 0).  Catches drift where
//     the bound widens from `in_range<1, 256>` to `in_range<0, 256>`,
//     silently admitting a zero-length name.
//   * That one is the upper-bound wide miss (= UINT16_MAX).  Catches
//     "drop the upper bound entirely" regression where
//     ValidSchemaNameLen degenerates from
//     `Refined<in_range<1, 256>, uint16_t>` into a plain
//     `uint16_t` typedef; under that drift any 16-bit value is
//     silently accepted at construction, name_buf[name_len] = '\0'
//     reads-and-writes past the 257-byte buffer for name_len > 256
//     (UB / heap-buffer-overflow under ASan), and the type-system
//     gate that fires before any fread call ever lands disappears.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.

#include <crucible/TraceLoader.h>

#include <cstdint>

int main() {
    // Constant evaluation forces the Refined ctor's pre clause
    // (`in_range<1, 256>(v)`) to be exercised at compile time.
    // v == 0 → 0 < 1 → predicate(v) == false → contract violation
    // → not a constant expression → ill-formed.
    constexpr crucible::ValidSchemaNameLen bad{uint16_t{0}};
    (void)bad;
    return 0;
}
