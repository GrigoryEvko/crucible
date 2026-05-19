// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-07 negative fixture #8/8:
// `fixy::substr::chaselev::mint_thief_session<Deque, Ctx>(ctx,
// handle)` rejects when the second (handle) parameter cannot
// bind to `typename Deque::ThiefHandle&`.
//
// Mirrors fixture #6 (owner_session_wrong_handle) on the thief
// side: proves that the ThiefHandle reference binding is
// preserved through the using-decl in Substr.h INDEPENDENTLY of
// the owner-side instantiation.
//
// `Deque` is supplied explicitly and IS a known
// ChaseLevSessionSurface.  `HotFgCtx` IS IsExecCtx (its row is
// `Row<>`, sufficient for the EmptyPermSet ThiefProto).  Both
// concepts pass; the second parameter `int` cannot bind to
// `Deque::ThiefHandle&` (a class reference).
//
// Distinct from fixture #7 (thief_session_non_ctx): #7 exercises
// the IsExecCtx prerequisite (first parameter slot); #8
// exercises the ThiefHandle reference binding (second parameter
// slot) AFTER IsExecCtx and the row-subset checks succeed.
//
// Expected diagnostic: "no matching function for call to
// 'mint_thief_session'" / "cannot convert" / "ThiefHandle" /
// "mint_thief_session".

#include <crucible/concurrent/PermissionedChaseLevDeque.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Substr.h>

namespace fchase = ::crucible::fixy::substr::chaselev;
namespace eff    = ::crucible::effects;

namespace neg_fixy_thief_session_wrong_handle {
struct UserTag {};
using Deque = ::crucible::concurrent::PermissionedChaseLevDeque<
    int, 16, UserTag>;
}

int main() {
    eff::HotFgCtx ctx{};
    int not_a_handle = 0;

    auto bad = fchase::mint_thief_session<
        neg_fixy_thief_session_wrong_handle::Deque>(ctx, not_a_handle);
    (void)bad;
    return 0;
}
