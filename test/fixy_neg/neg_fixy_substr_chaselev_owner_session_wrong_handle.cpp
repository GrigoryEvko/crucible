// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-07 negative fixture #6/8:
// `fixy::substr::chaselev::mint_owner_session<Deque, Ctx>(ctx,
// handle)` rejects when the second (handle) parameter cannot
// bind to `typename Deque::OwnerHandle&`.
//
// `Deque` is supplied explicitly and IS a known
// ChaseLevSessionSurface.  `HotFgCtx` IS IsExecCtx (its row is
// `Row<>`, which is a subrow of every ctx — sufficient for the
// EmptyPermSet OwnerProto<T>).  Both concepts pass; the second
// parameter `int` cannot bind to `Deque::OwnerHandle&` (a class
// reference).
//
// Distinct from fixture #5 (owner_session_non_ctx): #5 exercises
// the IsExecCtx prerequisite (first parameter slot); #6
// exercises the OwnerHandle reference binding (second parameter
// slot) AFTER IsExecCtx and the row-subset checks succeed.
//
// Expected diagnostic: "no matching function for call to
// 'mint_owner_session'" / "cannot convert" / "OwnerHandle" /
// "mint_owner_session".

#include <crucible/concurrent/PermissionedChaseLevDeque.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Substr.h>

namespace fchase = ::crucible::fixy::substr::chaselev;
namespace eff    = ::crucible::effects;

namespace neg_fixy_owner_session_wrong_handle {
struct UserTag {};
using Deque = ::crucible::concurrent::PermissionedChaseLevDeque<
    int, 16, UserTag>;
}

int main() {
    eff::HotFgCtx ctx{};
    int not_a_handle = 0;

    auto bad = fchase::mint_owner_session<
        neg_fixy_owner_session_wrong_handle::Deque>(ctx, not_a_handle);
    (void)bad;
    return 0;
}
