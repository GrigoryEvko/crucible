// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-231 HS14 fixture #5 (task-specified post-consume static-reject):
// calling `safety::OwnedMmap::release(...)` on an lvalue is rejected
// because `release()` is `&&`-ref-qualified — only rvalue carriers
// can invoke it.
//
// Why the `&&` qualifier IS the post-consume static-assert: an rvalue-
// ref-qualified member function can only be called on a carrier that
// is BEING moved (i.e. about to be consumed).  Once the call returns,
// the carrier is in the moved-from state (addr_ = MAP_FAILED) and a
// SECOND call would need ANOTHER `std::move` — but a moved-from value
// is still an lvalue at the C++ language level, so naive double-
// release fails the `&&` gate.  Combined with the internal sentinel
// swap (addr_ <- MAP_FAILED on the way out), double-munmap is
// structurally impossible.
//
// This is the canonical "consume once, then never again" C++ idiom.
// It generalises the V-225 Linear discipline: every consume-only
// method gets `&&`-qualified; the lvalue overload is absent (or
// deleted with reason), and the compiler enforces the "consumed"
// invariant statically.
//
// Mismatch class: lvalue-release (this is fixture #5).  Distinct from
// fixture #4 (missing-rationale-grant).  Together they form the two
// orthogonal gates on `release()`:
//
//   (4) WHAT may invoke release(): only LeakGrant-tagged callers.
//   (5) WHO may invoke release(): only rvalue carriers (consume-once).
//
// Either gate alone is insufficient; both together close the
// deliberate-leak escape hatch tightly enough that a code reviewer
// reading the diff can see EXACTLY where ownership leaves the type
// system and WHY.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "no matching function" / "cannot bind" / "rvalue" / "ref-qualif".

#include <crucible/safety/OwnedMmap.h>
#include <crucible/fixy/Mmap.h>  // pulls grant::leak::resource specialization

namespace {
    struct ProbeRegion {};
    struct ProbeProt   {};
    struct ProbeShare  {};
    struct ReleaseRationale {};
}  // namespace

int main() {
    using Owned     = ::crucible::safety::OwnedMmap<ProbeRegion, ProbeProt, ProbeShare>;
    using LeakGrant = ::crucible::fixy::grant::leak::resource<ReleaseRationale>;

    Owned region{};
    LeakGrant grant{};

    // Should FAIL: `release()` is `&&`-qualified.  `region` is an
    // lvalue (we did not `std::move()` it), so binding to the rvalue
    // implicit-object parameter is invalid.  The compiler emits one of:
    //   "no matching function for call to ‘release(...)’"
    //   "cannot bind rvalue reference of type ‘OwnedMmap&&’ to lvalue"
    //   "passing ‘const? OwnedMmap’ as ‘this’ argument discards ref-qualifier"
    //
    // The LeakGrant gate from fixture #4 IS satisfied here (we pass a
    // valid grant), isolating the lvalue-rejection class from the
    // missing-grant class.
    [[maybe_unused]] auto pair = region.release(grant);
    return 0;
}
