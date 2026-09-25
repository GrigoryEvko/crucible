// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Chase-Lev session mint negative fixture 1 of 8:
// `safety::proto::chaselev_session::mint_chaselev_owner<Deque>(deque,
// perm)` rejects when Deque is NOT a ChaseLevSessionSurface.
//
// Signature: `template <ChaseLevSessionSurface Deque>
//             constexpr auto mint_chaselev_owner(
//                 Deque& deque, Permission<typename Deque::owner_tag>&& perm)`
//
// Violation: pass `int` as Deque.  `int` lacks the
// ChaseLevSessionSurface concept's required nested types
// (owner_tag, thief_tag, OwnerHandle, ThiefHandle, value_type) and
// member functions (owner / thief).  The requires-clause fires at
// substitution time.
//
// Distinct from fixture #2 (wrong_perm_type): #1 exercises the
// ChaseLevSessionSurface concept gate on the deque parameter; #2
// exercises the Permission<owner_tag>&& binding on the second
// parameter AFTER the concept is satisfied.
//
// Expected diagnostic: "ChaseLevSessionSurface" / "constraints
// not satisfied" / "no matching function" / "mint_chaselev_owner".

#include <crucible/permissions/_Permission.h>
#include <crucible/sessions/_ChaseLevDequeSession.h>

namespace fchase = ::crucible::safety::proto::chaselev_session;
namespace saf = ::crucible::safety;

struct owner_tag_placeholder {};

int main() {
    int not_a_deque = 0;
    auto perm = saf::mint_permission_root<owner_tag_placeholder>();

    auto bad = fchase::mint_chaselev_owner(not_a_deque, std::move(perm));
    (void)bad;
    return 0;
}
