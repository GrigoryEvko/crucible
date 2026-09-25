// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Chase-Lev session mint negative fixture 5 of 8:
// `safety::proto::chaselev_session::mint_owner_session<Deque, Ctx>(
// ctx, handle)` rejects when the first (ctx) parameter is NOT an
// IsExecCtx.
//
// `Deque` is supplied explicitly (it appears in non-deduced
// position via `typename Deque::OwnerHandle&`) and IS a known
// ChaseLevSessionSurface.  The concept check on Deque passes;
// `Ctx` is deduced from the first argument as `int`; `IsExecCtx`
// rejects it.
//
// Distinct from fixture #6 (owner_session_wrong_handle): #5
// exercises the IsExecCtx prerequisite on the first parameter
// slot; #6 exercises the second (handle) parameter binding AFTER
// IsExecCtx is satisfied.
//
// Expected diagnostic: "IsExecCtx" / "constraints not satisfied"
// / "no matching function" / "mint_owner_session".

#include <crucible/concurrent/PermissionedChaseLevDeque.h>
#include <crucible/sessions/ChaseLevDequeSession.h>

namespace fchase = ::crucible::safety::proto::chaselev_session;

namespace neg_fixy_owner_session_non_ctx {
struct UserTag {};
using Deque = ::crucible::concurrent::PermissionedChaseLevDeque<int, 16, UserTag>;
}  // namespace neg_fixy_owner_session_non_ctx

int main() {
    int not_a_ctx = 0;
    neg_fixy_owner_session_non_ctx::Deque::OwnerHandle* handle = nullptr;

    auto bad = fchase::mint_owner_session<neg_fixy_owner_session_non_ctx::Deque>(not_a_ctx, *handle);
    (void)bad;
    return 0;
}
