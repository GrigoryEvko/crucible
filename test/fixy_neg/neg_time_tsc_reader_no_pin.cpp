// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-190 mint_tsc_reader, mismatch class #1 of 2:
// MISSING THE CpuPinned PROOF.
//
// A TSC read is per-core; mint_tsc_reader admits only a safety::CpuPinned
// proof (IsSingletonCpuPin).  Passing a bare `int` (no pin) fails the
// IsSingletonCpuPin constraint — you cannot mint a TSC reader without
// witnessing the affinity requirement.
//
// Distinct from neg_time_tsc_reader_multi_core.cpp (a non-singleton pin);
// here the argument is not a CpuPinned at all.
//
// Expected diagnostic: constraints not satisfied / IsSingletonCpuPin /
// no matching function / mint_tsc_reader.

#include <crucible/fixy/Time.h>
#include <crucible/effects/ExecCtx.h>

int main() {
    ::crucible::effects::ColdInitCtx init{};

    // Should FAIL: 7 is not a CpuPinned proof.
    auto reader = ::crucible::fixy::time::mint_tsc_reader<
        ::crucible::fixy::time::TscMode::Raw>(init, 7);

    return static_cast<int>(reader.read().peek());
}
