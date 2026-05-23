// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-231 HS14 fixture #2: OwnedMmap copy-assignment rejected.
//
// Sibling fixture to neg_owned_mmap_copy_rejected.cpp.  The
// copy-CONSTRUCTOR fixture pins the move-only ctor side; this
// fixture pins the OTHER half of the deleted-copy pair: the
// copy-ASSIGNMENT operator.  Both must be deleted independently —
// `delete("reason")` on the ctor without the matching delete on
// `operator=` would still allow `b = a;` to compile silently and
// trigger double-munmap on destruction.
//
// Two distinct rejection sites:
//   * Fixture #1 (ctor):       `OwnedMmap b{a};`     // copy-init
//   * Fixture #2 (this file):  `b = a;`              // copy-assign
//
// Together the pair pins the entire copy surface so a future
// hand-rolled `operator=(const OwnedMmap&)` can't be slipped in
// without first deleting the `delete("reason")` declaration.
//
// Mismatch class: deleted-copy-assignment with linearity-duplication
// reason.  Distinct from fixture #1 (ctor side) and fixture #3
// (cross-tag coercion).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "use of deleted function" / "deleted" / "copy" / "double-unmap".

#include <crucible/safety/OwnedMmap.h>

namespace {
    struct ProbeRegion {};
    struct ProbeProt   {};
    struct ProbeShare  {};
}  // namespace

int main() {
    using Owned = ::crucible::safety::OwnedMmap<ProbeRegion, ProbeProt, ProbeShare>;

    Owned source{};
    Owned target{};

    // Should FAIL: copy-assignment operator is deleted with the
    // structured reason string; OwnedMmap is exclusive.
    target = source;
    return 0;
}
