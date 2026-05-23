// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-187 CpuPinned, mismatch class #1 of 3:
// __rdtsc WITHOUT A PIN PROOF.
//
// A value whose bytes come from `__rdtsc` is only meaningful while the
// producing thread cannot migrate.  A TSC reader therefore requires a
// CpuPinned proof argument (the witness that the thread was pinned at
// construction).  Calling the reader with a bare value (no proof) MUST be a
// compile error — the `IsCpuPinned` gate has no matching candidate for a
// non-proof type.
//
// Distinct from neg_cpu_pinned_two_bit_mask_singleton.cpp (a singleton-mask
// gate) and neg_cpu_pinned_auto_on_hotpath.cpp (a posture gate); here the
// failure is the PROOF-REQUIRED gate: a non-CpuPinned argument is rejected.
//
// Expected diagnostic: constraints not satisfied / no matching function /
// IsCpuPinned / read_tsc.

#include <crucible/safety/CpuPinned.h>
#include <crucible/safety/IsCpuPinned.h>

using namespace crucible::safety;

// A TSC reader admits only a CpuPinned pin proof (passed by const-ref so the
// move-only proof is not consumed by the read).
template <typename Proof>
    requires ::crucible::safety::extract::IsCpuPinned<Proof>
[[nodiscard]] int read_tsc(Proof const& proof) {
    return proof.peek();
}

int main() {
    // Should FAIL: 123 is a bare int, not a CpuPinned pin proof — the
    // IsCpuPinned-constrained read_tsc has no matching candidate.
    return read_tsc(123);
}
