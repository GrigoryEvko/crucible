// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-021: the pass-key for CrashEvent's restricted ctor —
// WrapCrashReturnKey — has a PRIVATE default constructor that is
// friended only on `detail::WrapCrashReturnAuthorizer`.  External
// code MUST NOT be able to mint a key directly; this fixture
// witnesses the rejection by attempting `WrapCrashReturnKey{}` from
// a translation unit that is neither `wrap_crash_return` nor the
// authorizer.
//
// Together with neg_crash_event_external_construction.cpp this
// pins HS14: the pass-key idiom's brittleness target (parameter-
// order drift in the friend declaration) is now structurally
// witnessed at the call site rather than relying on far-away
// CrashEvent construction failures.
//
// Expected diagnostic family (one or more should match):
//   "is private within this context"  |  "private"  |
//   "WrapCrashReturnKey"

#include <crucible/bridges/CrashTransport.h>

[[maybe_unused]] void probe() {
    auto bad = ::crucible::safety::proto::WrapCrashReturnKey{};
    (void)bad;
}
