// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing SchemaTable::SizeCounter with the value
// SCHEMA_TABLE_CAP + 1 in a constexpr context — fires
// BoundedMonotonic<uint32_t, SCHEMA_TABLE_CAP>'s ctor pre-clause at
// the boundary edge.
//
// Per WRAP-SchemaTab-1 (#1003), SchemaTable::SizeCounter is
// safety::BoundedMonotonic<uint32_t, SCHEMA_TABLE_CAP=512>.  The
// ctor's `pre(!(T(Max) < initial))` (≡ initial <= Max) admits values
// in [0, 512] and rejects 513 and above.  Valid runtime sizes occupy
// [0, CAP) — entries[CAP] is past the array.  CAP itself is the
// "table full" sentinel that the silent-truncation runtime check
// catches at register_name (see #1009 WRAP-SchemaTab-7 for the abort
// upgrade); CAP+1 is never legitimate.
//
// In constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.
//
// Companion fixture: neg_schema_table_size_uint32_max.cpp
//   * This one is the boundary edge (= CAP + 1, off-by-two).
//     Catches drift where the bound widens from
//     `BoundedMonotonic<..., SCHEMA_TABLE_CAP>` to
//     `BoundedMonotonic<..., SCHEMA_TABLE_CAP + N>` for some N >= 1,
//     silently admitting registrations past the array.
//   * That one is the wide miss (= UINT32_MAX).  Catches
//     "BoundedMonotonic regressed to plain Monotonic" where the
//     upper bound disappears entirely and any uint32_t is admitted —
//     leaves register_name's `if (size >= CAP)` silent-truncate as
//     the only defense (works at runtime but loses the type-system
//     gate that fires before any register_name() call ever lands).
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate,
// each demonstrating a distinct mismatch class.

#include <crucible/ir001/SchemaTable.h>

#include <cstdint>

int main() {
    // Constant evaluation forces the BoundedMonotonic ctor's pre
    // clause `!(T(CAP) < initial)` to be exercised at compile time.
    // initial == CAP + 1 → CAP < CAP + 1 → predicate(initial) false
    // → contract violation → not a constant expression → ill-formed.
    constexpr crucible::SchemaTable::SizeCounter bad{
        static_cast<uint32_t>(crucible::SCHEMA_TABLE_CAP) + uint32_t{1}};
    (void)bad;
    return 0;
}
