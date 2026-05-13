// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing CKernelTable::SizeCounter with the value
// UINT32_MAX in a constexpr context — the wide-miss fixture for the
// CKERNEL_TABLE_CAP bound.
//
// Per WRAP-CKernel-2 (#890), CKernelTable::SizeCounter is
// safety::BoundedMonotonic<uint32_t, CKERNEL_TABLE_CAP=256>.
// 0xFFFFFFFF is past every plausible registration-counter value and
// would aliase to a "size" that overshoots the entries[] array by
// ~16M; the gate must reject.
//
// In constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.
//
// Companion fixture: neg_ckernel_table_size_above_cap.cpp
//   * That one is the boundary edge (= CAP + 1, off-by-two).
//   * This one is the wide miss (= UINT32_MAX).  Catches "drop the
//     bound entirely" regression where SizeCounter degenerates from
//     `BoundedMonotonic<uint32_t, CKERNEL_TABLE_CAP>` into a plain
//     `Monotonic<uint32_t>` (or an unwrapped uint32_t typedef);
//     under that drift any size value is silently accepted at
//     construction and the only remaining defense is register_op's
//     explicit `if (size >= CAP)` runtime check (works at runtime
//     but loses the type-system gate that fires before any
//     register_op() call ever lands).
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate,
// each demonstrating a distinct mismatch class.

#include <crucible/CKernel.h>

#include <climits>
#include <cstdint>

int main() {
    // Constant evaluation forces the BoundedMonotonic ctor's pre
    // clause `!(T(CAP) < initial)` to be exercised at compile time.
    // initial == UINT32_MAX → CAP < UINT32_MAX → predicate(initial)
    // false → contract violation → not a constant expression →
    // ill-formed.
    constexpr crucible::CKernelTable::SizeCounter bad{
        uint32_t{UINT32_MAX}};
    (void)bad;
    return 0;
}
