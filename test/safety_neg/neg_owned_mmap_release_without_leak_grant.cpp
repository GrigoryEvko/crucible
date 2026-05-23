// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-231 HS14 fixture #4 (task-specified release-without-grant):
// calling `safety::OwnedMmap::release(...)` with an argument that
// does NOT satisfy `IsLeakGrant<LeakGrant>` is a substitution
// failure ("no matching function").
//
// The `release()` method is the deliberate-leak escape hatch — it
// yields the (addr, length) pair to a kernel-owned subsystem that
// will close on its own schedule (perf_event_open ringbuf, XDP umem,
// io_uring fixed buffer).  Without the type-system gate, a caller
// could accidentally invoke `release()` thinking it means "clean
// release of ownership" rather than "deliberate kernel-owned leak"
// — silently leaking VA space.
//
// V-231 gates the call with `requires IsLeakGrant<LeakGrant>` so the
// only types that match are members of the
// `crucible::fixy::grant::leak::resource<RationaleTag>` family.  A
// random type (here, `int`) does NOT satisfy `IsLeakGrant` because
// the `is_leak_grant<int>` primary trait returns `std::false_type`.
//
// Mismatch class: missing-rationale-grant.  Distinct from fixture
// #1 (copy-ctor red), #2 (copy-assign red), #3 (cross-Tag red),
// and #5 (lvalue-release red).
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "no matching function" / "constraints not satisfied" /
//   "IsLeakGrant" / "is_leak_grant".

#include <crucible/safety/OwnedMmap.h>

#include <utility>

namespace {
    struct ProbeRegion {};
    struct ProbeProt   {};
    struct ProbeShare  {};
}  // namespace

int main() {
    using Owned = ::crucible::safety::OwnedMmap<ProbeRegion, ProbeProt, ProbeShare>;

    Owned region{};

    // Should FAIL: int does NOT satisfy `IsLeakGrant<int>` because
    // the primary `is_leak_grant<int>` returns false (no partial
    // specialization flips it true for fundamental types).  The
    // `requires IsLeakGrant<LeakGrant>` clause on `release` therefore
    // rejects the int substitution at substitution-failure time.
    //
    // Also uses std::move to put the carrier in rvalue category so
    // the `&&` ref-qualifier is satisfied — this isolates the
    // IsLeakGrant rejection from the lvalue rejection (see fixture
    // #5 for the lvalue-side mismatch class).
    [[maybe_unused]] auto pair = std::move(region).release(42);
    return 0;
}
