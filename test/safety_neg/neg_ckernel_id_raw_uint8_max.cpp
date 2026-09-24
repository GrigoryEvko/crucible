// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidCKernelIdRaw with UINT8_MAX in a
// constexpr context — the wide-miss fixture for the
// uint8_t → CKernelId widening gate.
//
// Per WRAP-CKernel-4 (#892), ValidCKernelIdRaw is
// safety::Refined<safety::bounded_above<NUM_KERNELS - 1>, uint8_t>.
// 0xFF is past every plausible kernel id and would alias to a
// CKernelId enumerator that does not exist; the gate must reject.
//
// In constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.
//
// Companion fixture: neg_ckernel_id_raw_at_sentinel.cpp
//   * That one is the boundary edge (= NUM_KERNELS, off-by-one).
//   * This one is the wide miss (= UINT8_MAX).  Catches "drop the
//     bound entirely" regression where ValidCKernelIdRaw degenerates
//     into a plain uint8_t alias (e.g. someone replaces the Refined
//     wrap with a `using ValidCKernelIdRaw = uint8_t;` typedef);
//     under that drift any byte from a corrupted Cipher file is
//     silently accepted and classify() returns garbage.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate,
// each demonstrating a distinct mismatch class.

#include <crucible/CKernel.h>

#include <climits>
#include <cstdint>

int main() {
    // Constant evaluation forces the Refined ctor's pre-clause to be
    // exercised at compile time.  v == 0xFF → predicate(v) == false →
    // contract violation → the expression is not a constant
    // expression → ill-formed.
    constexpr crucible::ValidCKernelIdRaw bad{uint8_t{UINT8_MAX}};
    (void)bad;
    return 0;
}
