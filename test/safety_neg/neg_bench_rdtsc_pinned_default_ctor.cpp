// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-196 RdtscPinned witness — mismatch class #1 of 2: NO PUBLIC
// DEFAULT CTOR.
//
// RdtscPinned declares `constexpr RdtscPinned(CpuId, mint_tag) noexcept`
// (private), which suppresses the implicit default constructor — and
// no public default ctor is provided.  The witness must be minted only
// via BenchHarness's friend-private path; default construction would
// produce a "valid-looking" RdtscPinned out of thin air, defeating the
// proof discipline.
//
// Distinct from neg_bench_rdtsc_pinned_mint_tag_access.cpp (private
// mint_tag access); here the failure is the absence of any public
// constructor that takes zero arguments.
//
// Expected diagnostic: "no matching function" / "deleted" /
// "RdtscPinned" / "no default constructor".

#include "bench_harness.h"

int main() {
    // Should FAIL: no public default constructor; the only ctor is
    // private and requires a mint_tag witness.
    bench::RdtscPinned witness;
    (void)witness;
    return 0;
}
