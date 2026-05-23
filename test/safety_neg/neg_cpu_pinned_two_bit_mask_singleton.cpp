// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-187 CpuPinned, mismatch class #2 of 3:
// 2-BIT MASK FAILS THE SINGLETON GATE.
//
// A TSC read is sound only when the thread is pinned to EXACTLY ONE core —
// `AffinityLattice::is_singleton(Mask)`.  A CpuPinned proof carrying a
// two-core mask (cores {0, 1}) is a real, constructible pin, but it does
// NOT satisfy the singleton requirement, so a TSC-reader gate that demands
// `Proof::is_singleton_pin` MUST reject it at compile time.  Reading the TSC
// while the thread can hop between two cores would silently read an
// unsynchronised counter.
//
// Distinct from neg_cpu_pinned_rdtsc_without_proof.cpp (no proof at all) and
// neg_cpu_pinned_auto_on_hotpath.cpp (a posture gate); here the proof EXISTS
// but its mask is not a singleton.
//
// Expected diagnostic: constraints not satisfied / no matching function /
// is_singleton_pin / require_singleton_pin.

#include <crucible/safety/CpuPinned.h>

#include <crucible/algebra/lattices/AffinityLattice.h>

using namespace crucible::safety;
using AffinityMask = ::crucible::algebra::lattices::AffinityMask;

// A TSC reader gate that requires a single-core pin.
template <typename Proof>
    requires (Proof::is_singleton_pin)
[[nodiscard]] int require_singleton_pin(Proof const& proof) {
    return proof.peek();
}

int main() {
    // A real, constructible proof — but its mask covers cores {0, 1}.
    auto two_core = mint_cpu_pinned<AffinityMask::range(0, 1),
                                    PinningPosture::PinnedExplicit, int>(42);

    // Should FAIL: a 2-core mask is not a singleton; the gate rejects it.
    return require_singleton_pin(two_core);
}
