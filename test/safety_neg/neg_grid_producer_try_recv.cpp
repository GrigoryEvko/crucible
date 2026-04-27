// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-A15 — PermissionedShardedGrid::ProducerHandle<I> exposes
// only try_push.  Calling try_recv must be a hard compile error.

#include <crucible/concurrent/PermissionedShardedGrid.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/PermissionGridGenerator.h>

namespace {

struct BadGrid {};

void exercise_producer_try_recv() {
    crucible::concurrent::PermissionedShardedGrid<int, 2, 2, 32, BadGrid> grid;
    auto whole = crucible::safety::permission_root_mint<
        crucible::concurrent::grid_tag::Whole<BadGrid>>();
    auto perms = crucible::safety::split_grid<
        crucible::concurrent::grid_tag::Whole<BadGrid>, 2, 2>(std::move(whole));
    auto p = grid.template producer<0>(
        std::move(std::get<0>(perms.producers)));

    // PRODUCER attempting RECV — try_recv is structurally absent.
    auto v = p.try_recv();
    (void)v;
}

}  // namespace

int main() { exercise_producer_try_recv(); return 0; }
