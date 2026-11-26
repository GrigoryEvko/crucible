// Negative compile test.  This file must fail to compile.
//
// A store of a frame takes a holder and a node of that one frame.  Two frames
// on two DAG arenas have two scope brands, so the store of a node of the
// second arena into the continuation of a node of the first arena finds no
// store: the node would name an arena that can end before its holder.
//
// Expected diagnostic: no store of the outer frame takes a node of the inner
// frame.

#include <crucible/MerkleDag.h>

int main() {
    ::fixy::BumpArena<crucible::DagArena> outer_arena = ::fixy::mint_bump_arena<crucible::DagArena>(
        ::fixy::Quantity<::fixy::unit::Byte, std::size_t>{std::size_t{4096}});
    ::fixy::BumpArena<crucible::DagArena> inner_arena = ::fixy::mint_bump_arena<crucible::DagArena>(
        ::fixy::Quantity<::fixy::unit::Byte, std::size_t>{std::size_t{4096}});
    ::foundation::core::with_arena_frame(outer_arena, [&inner_arena](auto& outer) noexcept -> void {
        const auto alloc = ::foundation::effects::testing::test().alloc;
        const auto holder = crucible::make_terminal(alloc, outer);
        ::foundation::core::with_arena_frame(inner_arena, [&outer, holder, alloc](auto& inner) noexcept -> void {
            const auto foreign = crucible::make_terminal(alloc, inner);
            outer.store(holder, &crucible::TraceNode::next, foreign);
        });
    });
    return 0;
}
