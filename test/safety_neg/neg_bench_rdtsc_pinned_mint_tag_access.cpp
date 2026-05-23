// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-196 RdtscPinned witness — mismatch class #2 of 2: PRIVATE
// mint_tag ACCESS.
//
// The only ctor on RdtscPinned takes a private nested `mint_tag` type
// as its second parameter.  Only `BenchHarness` (declared as friend)
// can name that tag and mint a witness.  An outside caller that tries
// to construct directly cannot synthesize the tag — its name is in
// RdtscPinned's private namespace.  This neg-compile catches the
// regression where a future change accidentally promotes mint_tag to
// public, making the witness forgeable.
//
// Distinct from neg_bench_rdtsc_pinned_default_ctor.cpp (no default
// ctor); here the failure is the inaccessibility of the mint-tag
// type name.
//
// Expected diagnostic: "private within this context" /
// "RdtscPinned::mint_tag" / "is private" / "inaccessible".

#include "bench_harness.h"

int main() {
    // Should FAIL: mint_tag is a private nested struct; only the
    // friend (BenchHarness) can name it.
    bench::RdtscPinned witness{bench::CpuId{0},
                               bench::RdtscPinned::mint_tag{}};
    (void)witness;
    return 0;
}
