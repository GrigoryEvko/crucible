// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-A15 — PermissionedShardedGrid::producer<I>() must reject I
// outside [0, M) at the FACTORY call boundary.  Out-of-range slot
// indices would silently access ProducerHandle<I> for a non-existent
// shard — undefined behavior in the underlying ShardedSpscGrid.
//
// Expected diagnostic substring (FRAMEWORK-CONTROLLED — taken from
// the static_assert in producer<I>()):
//   "I must be less than M"

#include <crucible/concurrent/PermissionedShardedGrid.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/PermissionGridGenerator.h>

namespace {

struct BadGrid {};

void exercise_oob_slot() {
    // 3-producer grid; asking for producer<5> must fail.
    crucible::concurrent::PermissionedShardedGrid<int, 3, 2, 32, BadGrid> grid;

    // We need a Permission<Producer<BadGrid, 5>> to even attempt
    // the call.  Mint a fake one via permission_root_mint — this is
    // unsafe outside test code but works for the negative witness:
    // even if the user manages to construct the type, the producer<I>
    // factory's static_assert fires.
    auto fake = crucible::safety::permission_root_mint<
        crucible::concurrent::grid_tag::Producer<BadGrid, 5>>();

    [[maybe_unused]] auto p = grid.template producer<5>(std::move(fake));
}

}  // namespace

int main() { exercise_oob_slot(); return 0; }
