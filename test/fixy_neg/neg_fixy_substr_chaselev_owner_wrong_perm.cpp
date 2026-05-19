// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-07 negative fixture #2/8:
// `fixy::substr::chaselev::mint_chaselev_owner<Deque>(deque, perm)`
// rejects when the second (perm) parameter cannot bind to
// `Permission<typename Deque::owner_tag>&&`.
//
// Distinct from fixture #1 (non_surface): #1 exercises the
// ChaseLevSessionSurface concept gate on the first (Deque)
// parameter; #2 exercises the second (perm) parameter binding
// AFTER the concept gate succeeds.
//
// `PermissionedChaseLevDeque<int, 16, UserTag>` is a known
// ChaseLevSessionSurface (witnessed by the static_assert at
// ChaseLevDequeSession.h:209).  The first parameter binds; the
// concept passes; the second parameter `int` cannot bind to
// `Permission<owner_tag>&&` (a class-typed rvalue reference).
//
// Expected diagnostic: "no matching function for call to
// 'mint_chaselev_owner'" / "cannot convert" / "Permission" /
// "mint_chaselev_owner".

#include <crucible/concurrent/PermissionedChaseLevDeque.h>
#include <crucible/fixy/Substr.h>
#include <crucible/permissions/Permission.h>

namespace fchase = ::crucible::fixy::substr::chaselev;

namespace neg_fixy_owner_wrong_perm {
struct UserTag {};
using Deque = ::crucible::concurrent::PermissionedChaseLevDeque<
    int, 16, UserTag>;
}

int main() {
    neg_fixy_owner_wrong_perm::Deque deque{};
    int not_a_perm = 0;

    auto bad = fchase::mint_chaselev_owner(deque, not_a_perm);
    (void)bad;
    return 0;
}
