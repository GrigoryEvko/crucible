// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-07 negative fixture #7/8:
// `fixy::substr::chaselev::mint_thief_session<Deque, Ctx>(ctx,
// handle)` rejects when the first (ctx) parameter is NOT an
// IsExecCtx.
//
// Mirrors fixture #5 (owner_session_non_ctx) on the thief side:
// proves that the IsExecCtx prerequisite is preserved through
// the using-decl in Substr.h INDEPENDENTLY of the owner-side
// instantiation.
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
#include <crucible/fixy/Substr.h>

namespace fchase = ::crucible::fixy::substr::chaselev;

namespace neg_fixy_thief_session_non_ctx {
struct UserTag {};
using Deque = ::crucible::concurrent::PermissionedChaseLevDeque<
    int, 16, UserTag>;
}

int main() {
    int not_a_ctx = 0;
    neg_fixy_thief_session_non_ctx::Deque::ThiefHandle* handle =
        nullptr;

    auto bad = fchase::mint_thief_session<
        neg_fixy_thief_session_non_ctx::Deque>(not_a_ctx, *handle);
    (void)bad;
    return 0;
}
