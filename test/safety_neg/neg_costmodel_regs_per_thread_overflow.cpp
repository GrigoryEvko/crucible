// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidRegsPerThread with the boundary-edge
// value 256 (= bounded_above<255> + 1) in constexpr context.
//
// Per #897 WRAP-CostModel-3, ValidRegsPerThread is
// safety::Refined<safety::bounded_above<uint16_t{255}>, uint16_t> — the
// per-thread register estimate fed into KernelConfig and from there to
// validate_config (constraint C2: cfg.regs_per_thread ≤
// hw.max_regs_per_thread) and sm_occupancy (divisor in
// hw.regs_per_sm / regs_per_thread).
//
// 255 is the ceiling on every shipped backend (NVIDIA Hopper / Blackwell
// SM_VERSION ≥ 80, AMD CDNA3+/RDNA3+); the corresponding
// HardwareProfile::max_regs_per_thread defaults to 255 in every preset.
// A KernelConfig{ .regs_per_thread = 999 } is structurally impossible
// hardware — but the bare uint16_t storage can hold it, and the
// optimizer would compute sm_occupancy as 65536/999 = 65 threads/SM,
// silently producing nonsense for downstream cost evaluation and
// MAP-Elites occupancy buckets.
//
// Companion fixture: neg_costmodel_regs_per_thread_max_uint16.cpp
//   * That one is the wide miss (UINT16_MAX = 65535, far past the
//     ceiling — catches drift where a caller passes a sentinel-like
//     "max" value via std::numeric_limits).
//   * This one is the boundary edge (256, exactly one past the
//     hardware ceiling) — catches off-by-one drift in callers that
//     compute regs_per_thread = something + 1 / shift and end up just
//     past the legal range.
//
// In constexpr context, a contract violation makes the expression non-
// constant per P1494R5 — using it where a constant is required is
// ill-formed.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.

#include <crucible/CostModel.h>

int main() {
    // Constant evaluation forces the Refined ctor's pre clause
    // (`bounded_above<255>(v)`) to be exercised at compile time.
    // 256 > 255 → bounded_above<255>(256) == false → contract violation
    // → not a constant expression → ill-formed.
    constexpr crucible::ValidRegsPerThread bad{uint16_t{256}};
    (void)bad;
    return 0;
}
