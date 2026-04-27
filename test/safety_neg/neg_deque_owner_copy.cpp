// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-A19 — PermissionedChaseLevDeque::OwnerHandle is move-only.
// Copying it would duplicate the linear Owner Permission, allowing
// two threads to race on push_bottom/pop_bottom (data race on
// bottom_) — the CL algorithm's single-owner contract would break
// silently with no compile error.

#include <crucible/concurrent/PermissionedChaseLevDeque.h>
#include <crucible/permissions/Permission.h>

namespace {

struct BadDeque {};

void exercise_owner_copy() {
    crucible::concurrent::PermissionedChaseLevDeque<int, 64, BadDeque> deque;
    auto perm = crucible::safety::permission_root_mint<
        crucible::concurrent::deque_tag::Owner<BadDeque>>();
    auto owner = deque.owner(std::move(perm));

    // Copy attempt — deleted with reason.
    auto owner2 = owner;
    (void)owner2;
}

}  // namespace

int main() { exercise_owner_copy(); return 0; }
