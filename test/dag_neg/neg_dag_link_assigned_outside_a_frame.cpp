// Negative compile test.  This file must fail to compile.
//
// The continuation of a node is a link slot of a frame (NodeLink of
// crucible/MerkleDag.h).  Only frame.store fills it, with a holder and a node
// of one frame, so the linked node is in the arena of the holder.  A family
// link can name a node of any DAG arena, so an assignment of one to the slot
// would join two arenas with no proof.  The slot has no assignment.
//
// Expected diagnostic: no assignment of a TraceLink to the slot.

#include <crucible/MerkleDag.h>

int main() {
    ::fixy::BumpArena<crucible::DagArena> arena = ::fixy::mint_bump_arena<crucible::DagArena>(
        ::fixy::Quantity<::fixy::unit::Byte, std::size_t>{std::size_t{4096}});
    ::foundation::core::with_arena_frame(arena, [](auto& frame) noexcept -> void {
        const auto alloc = ::foundation::effects::testing::test().alloc;
        const auto first = crucible::make_terminal(alloc, frame);
        const auto second = crucible::make_terminal(alloc, frame);
        first->next = crucible::TraceLink::some(second.weaken());
    });
    return 0;
}
