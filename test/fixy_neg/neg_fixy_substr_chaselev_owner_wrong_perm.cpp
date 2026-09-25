// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Chase-Lev session mint negative fixture 2 of 8:
// `safety::proto::chaselev_session::mint_chaselev_owner<Deque>(deque,
// perm)` rejects when the second (perm) parameter cannot bind to
// `Permission<typename Deque::owner_tag>&&`.
//
// Distinct from fixture #1 (non_surface): #1 exercises the
// ChaseLevSessionSurface concept gate on the first (Deque)
// parameter; #2 exercises the second (perm) parameter binding
// AFTER the concept gate succeeds.
//
// `PermissionedChaseLevDeque<int, 16, UserTag>` is a known
// ChaseLevSessionSurface (a static_assert in
// ChaseLevDequeSession.h witnesses it).  The first parameter binds; the
// concept passes; the second parameter `int` cannot bind to
// `Permission<owner_tag>&&` (a class-typed rvalue reference).
//
// Expected diagnostic: "no matching function for call to
// 'mint_chaselev_owner'" / "cannot convert" / "Permission" /
// "mint_chaselev_owner".

#include <crucible/concurrent/PermissionedChaseLevDeque.h>
#include <crucible/permissions/_Permission.h>
#include <crucible/sessions/ChaseLevDequeSession.h>

namespace fchase = ::crucible::safety::proto::chaselev_session;

namespace neg_fixy_owner_wrong_perm {
struct UserTag {};
using Deque = ::crucible::concurrent::PermissionedChaseLevDeque<int, 16, UserTag>;
}  // namespace neg_fixy_owner_wrong_perm

int main() {
    neg_fixy_owner_wrong_perm::Deque deque{};
    int not_a_perm = 0;

    auto bad = fchase::mint_chaselev_owner(deque, not_a_perm);
    (void)bad;
    return 0;
}
