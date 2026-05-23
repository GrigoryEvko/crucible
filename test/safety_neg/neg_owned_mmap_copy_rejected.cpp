// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-231 HS14 fixture #1: OwnedMmap copy ctor rejected.
//
// `safety::OwnedMmap<Tag, Prot, Share>` owns a `(void* addr,
// size_t len)` pair exclusively — copying the wrapper would alias
// the underlying virtual-address region and trigger double-munmap
// on destruction (the inverse of the double-free / double-fclose
// bug class).  The wrapper deletes its copy ctor with the structured
// reason string ("mmap region is unique; copy would double-unmap on
// destruction"); attempting to copy-construct fails substitution.
//
// Pre-V-231 the 7 perf hubs (src/perf/{SenseHub,PmuSample,SchedSwitch,
// SchedTpBtf,SyscallLatency,SyscallTpBtf,LockContention}.cpp) used raw
// `(void*, size_t)` pairs with explicit ::munmap on every early-return
// path; the leak / double-free bug class was an added early-return
// that forgot the munmap (leak) OR added it twice (double-unmap).
// This fixture pins the RAII guarantee at the type level: a future
// hand-rolled copy can't be slipped in without first deleting the
// `delete("reason")` declaration in safety/OwnedMmap.h.
//
// Mismatch class: deleted-copy with linearity-duplication reason —
// region uniqueness (the underlying VA range is unique; copy would
// double-unmap).  Distinct from fixture #2 (copy-assign) and from
// fixture #3 (cross-tag coercion).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "use of deleted function" / "deleted" / "copy" / "double-unmap".

#include <crucible/safety/OwnedMmap.h>

namespace {
    // Distinct empty struct tag — per-call-site identity discipline.
    struct ProbeRegion {};

    // Phantom Prot / Share — safety::OwnedMmap never interprets them;
    // the test only needs distinct types to satisfy the template.
    struct ProbeProt  {};
    struct ProbeShare {};
}  // namespace

int main() {
    // Default-construct an empty region (no ::mmap syscall invoked
    // — addr_ is MAP_FAILED sentinel by NSDMI).  The compile error
    // fires on the copy attempt, not on construction.
    ::crucible::safety::OwnedMmap<ProbeRegion, ProbeProt, ProbeShare> original{};

    // Should FAIL: copy ctor is deleted; OwnedMmap is exclusive.
    [[maybe_unused]] ::crucible::safety::OwnedMmap<ProbeRegion, ProbeProt, ProbeShare>
        alias{original};
    return 0;
}
