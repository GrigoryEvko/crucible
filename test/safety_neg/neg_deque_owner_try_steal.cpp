// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-A19 — PermissionedChaseLevDeque::OwnerHandle exposes
// try_push and try_pop only.  try_steal is structurally absent —
// the owner has the FAST pop_bottom path; calling steal_top on the
// owner thread is just slower for the same outcome (and a sign of
// confused intent).

#include <crucible/concurrent/PermissionedChaseLevDeque.h>
#include <crucible/permissions/Permission.h>

namespace {

struct BadDeque {};

void exercise_owner_try_steal() {
    crucible::concurrent::PermissionedChaseLevDeque<int, 64, BadDeque> deque;
    auto perm = crucible::safety::mint_permission_root<
        crucible::concurrent::deque_tag::Owner<BadDeque>>();
    auto owner = deque.owner(std::move(perm));

    auto v = owner.try_steal();
    (void)v;
}

}  // namespace

int main() { exercise_owner_try_steal(); return 0; }
