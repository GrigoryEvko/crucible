// The links of the Merkle DAG and the kind-checked doors of its nodes.

#include <crucible/MerkleDag.h>
#include "test_assert.h"

#include <cstdint>

[[gnu::cold]] int main() {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena arena(1 << 16);

    ::foundation::core::FixedArray<crucible::TraceEntry, 1> ops{};
    ops.at(::fixy::index<0>()).schema_hash = crucible::SchemaHash{0x1111};
    crucible::RegionNode& region = *crucible::make_region(test.alloc, arena, ops.view());
    crucible::TraceNode& terminal = *crucible::make_terminal(test.alloc, arena);
    crucible::LoopNode& loop =
        *crucible::make_loop(test.alloc, arena, region, crucible::ContentHash{0x5EED},
                             ::fixy::View<crucible::FeedbackEdge>{}, crucible::LoopTermKind::REPEAT, 2);

    // A link takes one pointer, and a link with no value is the default.
    static_assert(sizeof(crucible::TraceLink) == sizeof(void*));
    assert(crucible::TraceLink{}.is_none());

    // make_link gives the full link to the node.  is_link_to compares the
    // address of the linked node with the address of the node.
    const crucible::TraceLink to_region = crucible::make_link(region);
    assert(to_region.is_some());
    assert(crucible::is_link_to(to_region, region));
    assert(!crucible::is_link_to(to_region, terminal));
    assert(!crucible::is_link_to(crucible::TraceLink{}, region));

    // A copy of a link reaches the same node.
    crucible::TraceLink cursor{};
    cursor = to_region;
    assert(crucible::is_link_to(cursor, region));

    // make_loop links its body.
    assert(crucible::is_link_to(loop.body, region));

    // The door of a loop gives the node itself for a loop, and no value for
    // each other kind.
    assert(loop.as_loop().is_some());
    for (const ::fixy::Ref<crucible::LoopNode>& door_loop : loop.as_loop()) {
        assert(::fixy::same_object(door_loop, ::fixy::to(loop)));
    }
    assert(region.as_loop().is_none());
    assert(terminal.as_loop().is_none());

    crucible::test::pass("test_merkle_links: all tests passed\n");
    return 0;
}
