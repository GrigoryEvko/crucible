// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidRegsPerThread with the wide-miss value
// UINT16_MAX (= 65535) in constexpr context — far past the hardware
// register-per-thread ceiling.
//
// Per #897 WRAP-CostModel-3, ValidRegsPerThread is
// safety::Refined<safety::bounded_above<uint16_t{255}>, uint16_t>.  The
// uint16_t storage admits values up to 65535 by C++ language rules, but
// the structural ceiling on every shipped GPU backend is 255.  A
// regs_per_thread = 65535 escaping into sm_occupancy would compute
// hw.regs_per_sm / 65535 = 1 thread per SM, classify the kernel as
// catastrophically register-pressured, and route MAP-Elites toward a
// zero-occupancy archive cell that doesn't correspond to any real
// silicon — corrupting the calibrator's training data.
//
// Companion fixture: neg_costmodel_regs_per_thread_overflow.cpp
//   * That one is the boundary edge (256, exactly one past the
//     ceiling) — catches off-by-one drift.
//   * This one is the wide miss (UINT16_MAX, the storage type's max) —
//     catches drift where a caller passes std::numeric_limits<uint16_t>::max(),
//     a memcpy of an uninitialised uint16_t (≈ stack garbage), or
//     a deserialiser that read a corrupt cost-model snapshot.
//
// In constexpr context, a contract violation makes the expression non-
// constant per P1494R5 — using it where a constant is required is
// ill-formed.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.

#include <crucible/CostModel.h>

#include <climits>

int main() {
    // Constant evaluation forces the Refined ctor's pre clause
    // (`bounded_above<255>(v)`) to be exercised at compile time.
    // UINT16_MAX = 65535 > 255 → bounded_above<255>(65535) == false →
    // contract violation → not a constant expression → ill-formed.
    constexpr crucible::ValidRegsPerThread bad{uint16_t{UINT16_MAX}};
    (void)bad;
    return 0;
}
