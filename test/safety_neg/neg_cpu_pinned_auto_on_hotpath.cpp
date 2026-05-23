// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-187 CpuPinned, mismatch class #3 of 3:
// PinnedAuto REJECTED AT A HOTPATH STANCE.
//
// PinningPosture is ordered by pin-strength: NotPinned ⊏ PinnedAuto ⊏
// PinnedExplicit.  PinnedAuto (a best-effort / scheduler-incidental pin) can
// still migrate, incurring a latency spike.  A HotPath stance therefore
// requires the strongest posture, PinnedExplicit — a `meets_posture<
// PinnedExplicit>` gate.  A PinnedAuto proof does NOT meet that floor and
// MUST be rejected at compile time, so a latency-critical path is never
// silently pinned to a migratable core.
//
// Distinct from neg_cpu_pinned_rdtsc_without_proof.cpp (no proof) and
// neg_cpu_pinned_two_bit_mask_singleton.cpp (a non-singleton mask); here the
// proof IS a valid singleton pin but its POSTURE is too weak for HotPath.
//
// Expected diagnostic: constraints not satisfied / no matching function /
// meets_posture / on_hot_path.

#include <crucible/safety/CpuPinned.h>

#include <crucible/algebra/lattices/AffinityLattice.h>

using namespace crucible::safety;
using AffinityMask = ::crucible::algebra::lattices::AffinityMask;

// A HotPath consumer admits only an EXPLICIT pin (auto can migrate).
template <typename Proof>
    requires (Proof::template meets_posture<PinningPosture::PinnedExplicit>)
[[nodiscard]] int on_hot_path(Proof const& proof) {
    return proof.peek();
}

int main() {
    // A valid single-core pin, but only auto-pinned (can still migrate).
    auto auto_pin = mint_cpu_pinned<AffinityMask::single(0),
                                    PinningPosture::PinnedAuto, int>(42);

    // Should FAIL: PinnedAuto does not meet the PinnedExplicit HotPath floor.
    return on_hot_path(auto_pin);
}
