// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-A15 — PermissionedShardedGrid::ConsumerHandle<J> exposes
// only try_recv.  Calling try_push must be a hard compile error.

#include <crucible/concurrent/PermissionedShardedGrid.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/PermissionGridGenerator.h>

namespace {

struct BadGrid {};

void exercise_consumer_try_push() {
    crucible::concurrent::PermissionedShardedGrid<int, 2, 2, 32, BadGrid> grid;
    auto whole = crucible::safety::mint_permission_root<
        crucible::concurrent::grid_tag::Whole<BadGrid>>();
    auto perms = crucible::safety::split_grid<
        crucible::concurrent::grid_tag::Whole<BadGrid>, 2, 2>(std::move(whole));
    auto c = grid.template consumer<0>(
        std::move(std::get<0>(perms.consumers)));

    // CONSUMER attempting PUSH — try_push is structurally absent.
    c.try_push(42);
}

}  // namespace

int main() { exercise_consumer_try_push(); return 0; }
