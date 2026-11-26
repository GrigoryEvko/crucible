// Negative compile test.  This file must fail to compile.
//
// The ops of a region are a run slot of a frame (DagRunSlot of
// crucible/MerkleDag.h).  Only frame.store fills it, with a holder and a run
// of one frame, so the ops are in the arena of the region.  A run of the
// family can name elements of any DAG arena, so an assignment of one to the
// slot would join two arenas with no proof.  The slot has no assignment.
//
// Expected diagnostic: no assignment of an ArenaRun to the slot.

#include <crucible/MerkleDag.h>

int main() {
    ::fixy::BumpArena<crucible::DagArena> arena = ::fixy::mint_bump_arena<crucible::DagArena>(
        ::fixy::Quantity<::fixy::unit::Byte, std::size_t>{std::size_t{4096}});
    ::foundation::core::with_arena_frame(arena, [](auto& frame) noexcept -> void {
        const auto alloc = ::foundation::effects::testing::test().alloc;
        const auto region = crucible::mint_dag_node<crucible::RegionNode>(alloc, frame);
        auto ops = frame.template build_array<crucible::TraceEntry>(
            alloc, ::fixy::ElementCount{1}, [](::fixy::RunStart) noexcept { return crucible::TraceEntry{}; });
        region->ops = ops.weaken();
    });
    return 0;
}
