// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-07 negative fixture #5/8:
// `fixy::substr::chaselev::mint_owner_session<Deque, Ctx>(ctx,
// handle)` rejects when the first (ctx) parameter is NOT an
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
#include <crucible/fixy/Substr.h>

namespace fchase = ::crucible::fixy::substr::chaselev;

namespace neg_fixy_owner_session_non_ctx {
struct UserTag {};
using Deque = ::crucible::concurrent::PermissionedChaseLevDeque<
    int, 16, UserTag>;
}

int main() {
    int not_a_ctx = 0;
    neg_fixy_owner_session_non_ctx::Deque::OwnerHandle* handle =
        nullptr;

    auto bad = fchase::mint_owner_session<
        neg_fixy_owner_session_non_ctx::Deque>(not_a_ctx, *handle);
    (void)bad;
    return 0;
}
