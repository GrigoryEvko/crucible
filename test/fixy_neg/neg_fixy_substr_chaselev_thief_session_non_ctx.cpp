// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Chase-Lev session mint negative fixture 7 of 8:
// `safety::proto::chaselev_session::mint_thief_session<Deque, Ctx>(
// ctx, handle)` rejects when the first (ctx) parameter is NOT an
// IsExecCtx.
//
// Mirrors fixture #5 (owner_session_non_ctx) on the thief side:
// proves that the IsExecCtx prerequisite fires INDEPENDENTLY of
// the owner-side instantiation.
//
// `Deque` is supplied explicitly and IS a known
// ChaseLevSessionSurface.  `Ctx` is deduced from the first
// argument as `int`; `IsExecCtx` rejects it.
//
// Distinct from fixture #8 (thief_session_wrong_handle): #7
// exercises the IsExecCtx prerequisite (first parameter slot);
// #8 exercises the ThiefHandle reference binding (second
// parameter slot) AFTER IsExecCtx is satisfied.
//
// Expected diagnostic: "IsExecCtx" / "constraints not satisfied"
// / "no matching function" / "mint_thief_session".

#include <crucible/concurrent/PermissionedChaseLevDeque.h>
#include <crucible/sessions/ChaseLevDequeSession.h>

namespace fchase = ::crucible::safety::proto::chaselev_session;

namespace neg_fixy_thief_session_non_ctx {
struct UserTag {};
using Deque = ::crucible::concurrent::PermissionedChaseLevDeque<int, 16, UserTag>;
}  // namespace neg_fixy_thief_session_non_ctx

int main() {
    int not_a_ctx = 0;
    neg_fixy_thief_session_non_ctx::Deque::ThiefHandle* handle = nullptr;

    auto bad = fchase::mint_thief_session<neg_fixy_thief_session_non_ctx::Deque>(not_a_ctx, *handle);
    (void)bad;
    return 0;
}
