// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-190 mint_tsc_reader, mismatch class #2 of 2:
// MULTI-CORE (NON-SINGLETON) PIN.
//
// A TSC read is comparable only within ONE core.  A CpuPinned proof to a
// MULTI-core mask (popcount > 1) still permits migration between those
// cores, so IsSingletonCpuPin is false and the reader mint rejects it —
// forcing a single-core pin (AffinityMask::single(...)).
//
// Distinct from neg_time_tsc_reader_no_pin.cpp (no CpuPinned at all); here
// the argument IS a CpuPinned, but pinned to two cores.
//
// Expected diagnostic: constraints not satisfied / IsSingletonCpuPin /
// no matching function / mint_tsc_reader.

#include <crucible/fixy/Time.h>
#include <crucible/effects/ExecCtx.h>

int main() {
    ::crucible::effects::ColdInitCtx init{};

    // A proof pinned to cores {0,1} — popcount 2, not a singleton.
    auto multi = ::crucible::safety::mint_cpu_pinned<
        ::crucible::algebra::lattices::AffinityMask::range(0, 1),
        ::crucible::safety::PinningPosture::PinnedExplicit, int>(0);

    // Should FAIL: a multi-core pin does not satisfy IsSingletonCpuPin.
    auto reader = ::crucible::fixy::time::mint_tsc_reader<
        ::crucible::fixy::time::TscMode::Raw>(init, std::move(multi));

    return static_cast<int>(reader.read().peek());
}
