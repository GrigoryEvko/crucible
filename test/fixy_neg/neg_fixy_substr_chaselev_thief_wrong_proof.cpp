// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Chase-Lev session mint negative fixture 4 of 8:
// `safety::proto::chaselev_session::mint_chaselev_thief<Deque>(deque,
// proof)` (two-arg overload) rejects when the second (proof)
// parameter cannot bind to
// `SharedPermission<typename Deque::thief_tag>`.
//
// Distinct from fixture #3 (thief_non_surface): #3 exercises the
// single-arg overload's concept gate; #4 exercises the two-arg
// overload's proof template-class binding AFTER the concept is
// satisfied.
//
// `PermissionedChaseLevDeque<int, 16, UserTag>` is a known
// ChaseLevSessionSurface.  The first parameter binds; the
// concept passes; the second parameter `int` cannot bind to
// `SharedPermission<thief_tag>` (a class type).
//
// Expected diagnostic: "no matching function for call to
// 'mint_chaselev_thief'" / "cannot convert" / "SharedPermission"
// / "mint_chaselev_thief".

#include <crucible/concurrent/_PermissionedChaseLevDeque.h>
#include <crucible/permissions/_Permission.h>
#include <crucible/sessions/_ChaseLevDequeSession.h>

namespace fchase = ::crucible::safety::proto::chaselev_session;

namespace neg_fixy_thief_wrong_proof {
struct UserTag {};
using Deque = ::crucible::concurrent::PermissionedChaseLevDeque<int, 16, UserTag>;
}  // namespace neg_fixy_thief_wrong_proof

int main() {
    neg_fixy_thief_wrong_proof::Deque deque{};
    int not_a_proof = 0;

    auto bad = fchase::mint_chaselev_thief(deque, not_a_proof);
    (void)bad;
    return 0;
}
