// Negative compile test.  This file must fail to compile.
//
// The plan of a region is a link slot of a frame (PlanSlot of
// crucible/MerkleDag.h).  Only frame.store fills it, with a region and a plan
// of one frame, so the plan is in the arena of the region.  A PlanLink can
// name a plan in any DAG arena, so an assignment of one to the slot would
// join two arenas with no proof.  The slot has no assignment.
//
// Expected diagnostic: no assignment of a PlanLink to the slot.

#include <crucible/MerkleDag.h>

int main() {
    ::fixy::BumpArena<crucible::DagArena> arena = ::fixy::mint_bump_arena<crucible::DagArena>(
        ::fixy::Quantity<::fixy::unit::Byte, std::size_t>{std::size_t{4096}});
    ::foundation::core::with_arena_frame(arena, [](auto& frame) noexcept -> void {
        const auto alloc = ::foundation::effects::testing::test().alloc;
        const auto region = crucible::mint_dag_node<crucible::RegionNode>(alloc, frame);
        const auto plan = frame.template make<crucible::MemoryPlan>(alloc);
        region->plan = crucible::PlanLink{::fixy::some(crucible::DagRef<crucible::MemoryPlan const>{plan.weaken()})};
    });
    return 0;
}
