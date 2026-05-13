// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing CKernelTable::SizeCounter with the value
// CKERNEL_TABLE_CAP + 1 in a constexpr context — fires
// BoundedMonotonic<uint32_t, CKERNEL_TABLE_CAP>'s ctor pre-clause
// at the boundary edge.
//
// Per WRAP-CKernel-2 (#890), CKernelTable::SizeCounter is
// safety::BoundedMonotonic<uint32_t, CKERNEL_TABLE_CAP=256>.  The
// ctor's `pre(!(T(Max) < initial))` (≡ initial <= Max) admits values
// in [0, 256] and rejects 257 and above.  Valid runtime sizes occupy
// [0, CAP) — entries[CAP] is past the array.  CAP itself is the
// "table full" sentinel that the explicit overflow check catches at
// register_op; CAP+1 is never legitimate.
//
// In constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.
//
// Companion fixture: neg_ckernel_table_size_uint32_max.cpp
//   * This one is the boundary edge (= CAP + 1, off-by-two).
//     Catches drift where the bound widens from
//     `BoundedMonotonic<..., CKERNEL_TABLE_CAP>` to
//     `BoundedMonotonic<..., CKERNEL_TABLE_CAP + N>` for some N >= 1,
//     silently admitting registrations past the array.
//   * That one is the wide miss (= UINT32_MAX).  Catches
//     "BoundedMonotonic regressed to plain Monotonic" where the
//     upper bound disappears entirely and any uint32_t is admitted —
//     leaves register_op's explicit `if (size >= CAP)` as the only
//     defense (works at runtime but loses the type-system gate).
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate,
// each demonstrating a distinct mismatch class.

#include <crucible/ir001/CKernel.h>

#include <cstdint>

int main() {
    // Constant evaluation forces the BoundedMonotonic ctor's pre
    // clause `!(T(CAP) < initial)` to be exercised at compile time.
    // initial == CAP + 1 → CAP < CAP + 1 → predicate(initial) false
    // → contract violation → not a constant expression → ill-formed.
    constexpr crucible::CKernelTable::SizeCounter bad{
        static_cast<uint32_t>(crucible::CKERNEL_TABLE_CAP) + uint32_t{1}};
    (void)bad;
    return 0;
}
