// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidCKernelIdRaw with the value of the
// CKernelId::NUM_KERNELS sentinel (currently 147) in a constexpr
// context — fires Refined<bounded_above<NUM_KERNELS - 1>, uint8_t>'s
// pre-clause exactly at the boundary edge.
//
// Per WRAP-CKernel-4 (#892), ValidCKernelIdRaw is
// safety::Refined<safety::bounded_above<NUM_KERNELS - 1>, uint8_t>,
// the typed gate at every uint8_t → CKernelId widening site
// (Cipher deserialize, TraceLoader, FFI bridges).  The bound admits
// 0 (OPAQUE) through static_cast<uint8_t>(COMM_BARRIER) and rejects
// NUM_KERNELS (the count sentinel, never a real opcode) and
// everything above.
//
// In constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.
//
// Companion fixture: neg_ckernel_id_raw_uint8_max.cpp
//   * This one is the boundary edge (= NUM_KERNELS, off-by-one).
//     Catches drift where the bound widens from `NUM_KERNELS - 1` to
//     `NUM_KERNELS`, silently admitting the sentinel as if it were a
//     real opcode (which would cascade into classify() returning the
//     sentinel and downstream switches falling through to UB).
//   * That one is the wide miss (= UINT8_MAX).  Catches "drop the
//     bound entirely" regression where ValidCKernelIdRaw degenerates
//     into a plain uint8_t alias and admits any byte.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate,
// each demonstrating a distinct mismatch class.

#include <crucible/ir001/CKernel.h>

#include <cstdint>

int main() {
    // Constant evaluation forces the Refined ctor's pre clause
    // (`bounded_above<NUM_KERNELS - 1>(v)`) to be exercised at
    // compile time.  v == NUM_KERNELS → predicate(v) == false →
    // contract violation → the expression is not a constant
    // expression → ill-formed initialization of constexpr local.
    constexpr crucible::ValidCKernelIdRaw bad{
        static_cast<uint8_t>(crucible::CKernelId::NUM_KERNELS)};
    (void)bad;
    return 0;
}
