// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-060 fixture #2 — the owner session's Recv branch is owner
// pop_bottom, not thief steal_top.  Trying to use the thief borrowed
// transport through the owner protocol is rejected structurally.

#include <crucible/concurrent/PermissionedChaseLevDeque.h>
#include <crucible/permissions/Permission.h>
#include <crucible/sessions/ChaseLevDequeSession.h>

#include <utility>

namespace concur = crucible::concurrent;
namespace safety = crucible::safety;
namespace ses = crucible::safety::proto::chaselev_session;

namespace {
struct Tag {};
using Deque = concur::PermissionedChaseLevDeque<int, 64, Tag>;
}

int main() {
    Deque deque;
    auto owner_perm = safety::mint_permission_root<Deque::owner_tag>();
    auto owner = ses::mint_chaselev_owner<Deque>(deque, std::move(owner_perm));
    auto psh = ses::mint_owner_session<Deque>(owner);
    auto owner_pop = std::move(psh).select_local<ses::owner_pop_branch>();
    [[maybe_unused]] auto bad =
        std::move(owner_pop).recv(ses::blocking_steal_borrowed);
    return 0;
}
