// SPDX-License-Identifier: Apache-2.0
//
// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture for fix-04 (§XXI Universal Mint Pattern compliance).
//
// Cross-header companion to neg_session_handle_ctor_public_bypass.cpp.
// That fixture pins the Session.h Send<T, R> specialization; THIS one
// pins the SessionCrash.h Stop_g<C> crash-stop specialization, proving
// the private-ctor + detail::make_session_handle-friend closure reaches
// across headers (the factory is forward-declared in Session.h, which
// SessionCrash.h includes).
//
// Premise: SessionHandle<Stop_g<C>, Resource, LoopCtx> is a terminal
// crash-stop state reached only through mint_session_handle / the
// typestate transitions (detail::step_to_next).  A direct
// `SessionHandle<Stop_g<CrashClass::Abort>, Res, void>{res}` would
// fabricate a crash-stop handle without going through the factory's
// gate — the §XXI bypass fix-04 closes.
//
// Fix shape (fix-04): value ctor moved to `private:`, only
// detail::make_session_handle friended.  Build MUST fail; diagnostic
// MUST contain "is private".

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCrash.h>

#include <utility>

namespace proto = crucible::safety::proto;

namespace {

// Plain value type satisfies SessionResource (handle owns it by value).
struct FakeChannel {
    int sentinel = 0;
};

}  // namespace

int main() {
    // ── The §XXI bypass attempt ────────────────────────────────────
    //
    // Direct construction of a crash-stop SessionHandle bypasses
    // mint_session_handle / step_to_next.  With the fix-04 fix the value
    // ctor is private and this line is ill-formed.
    proto::SessionHandle<proto::Stop, FakeChannel, void>
        crash_handle{FakeChannel{.sentinel = 7}};
    (void)crash_handle;

    return 0;
}
