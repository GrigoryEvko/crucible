// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A4-014 fixture: bare `mint_session<Proto>(resource)` via fixy::
// alias (the second of the two `= delete`d overloads — companion to
// neg_fixy_sess_mint_session_deleted.cpp which exercises the
// (ctx, resource) form).
//
// Violation: `proto::mint_session<Proto>(resource)` is `= delete`d in
// sessions/SessionMint.h:986 with a diagnostic directing callers to
// `mint_permissioned_session<Proto>(ctx, resource, perms...)`.  Routing
// through `fixy::sess::mint_session` re-exposes the deletion via the
// using-decl at fixy/Sess.h:238 — this fixture proves the deletion
// diagnostic surfaces identically through the fixy:: namespace path
// for the bare overload (no Ctx parameter).
//
// HS14 floor: A4-014 audit identified the §XXI grep-target as diluted
// by the using-decl; this is the second of the two required neg-
// compile fixtures pinning that ALL deleted overloads stay rejected
// through fixy::, so a future un-delete breaks here rather than
// silently re-admitting a non-Permissioned construction surface.
//
// Expected diagnostic: "is removed" / "mint_permissioned_session".

#include <crucible/fixy/Sess.h>

namespace neg_fixy_sess_mint_session_bare_deleted {
struct DummyResource {};
}

namespace fsess = ::crucible::fixy::sess;

int main() {
    using SendInt = fsess::Send<int, fsess::End>;
    neg_fixy_sess_mint_session_bare_deleted::DummyResource res{};
    // Calling the bare-resource =deleted overload via fixy:: alias.
    // No Ctx — directly invokes mint_session<Proto>(Resource&&).
    fsess::mint_session<SendInt>(std::move(res));
    return 0;
}
