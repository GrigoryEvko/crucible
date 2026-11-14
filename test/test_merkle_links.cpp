// The links of the Merkle DAG and the kind-checked doors of its nodes.

#include <crucible/MerkleDag.h>
#include <crucible/Serialize.h>
#include "test_assert.h"

#include <cstddef>
#include <cstdint>

namespace {

// A branch of two arms, of the values 1 and 2, in the arena.
[[nodiscard]] crucible::BranchNode& make_two_arm_branch(::foundation::effects::Alloc alloc, ::fixy::BumpArena& arena,
                                                        crucible::TraceLink first,
                                                        crucible::TraceLink second) noexcept {
    crucible::BranchNode& branch = *arena.make<crucible::BranchNode>(alloc);
    branch.kind = crucible::TraceNodeKind::BRANCH;
    branch.arms = arena.array<crucible::BranchNode::Arm>(alloc, 2, crucible::BranchNode::Arm{});
    *branch.arms.get(0).expect("the branch holds two arms") = crucible::BranchNode::Arm{.value = 1, .target = first};
    *branch.arms.get(1).expect("the branch holds two arms") = crucible::BranchNode::Arm{.value = 2, .target = second};
    return branch;
}

}  // namespace

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

    // ── The arms of a branch ──────────────────────────────────────────
    ::foundation::core::FixedArray<crucible::TraceEntry, 1> ops_a{};
    ops_a.at(::fixy::index<0>()).schema_hash = crucible::SchemaHash{0xA1};
    ::foundation::core::FixedArray<crucible::TraceEntry, 1> ops_b{};
    ops_b.at(::fixy::index<0>()).schema_hash = crucible::SchemaHash{0xB2};
    crucible::RegionNode& region_a = *crucible::make_region(test.alloc, arena, ops_a.view());
    crucible::RegionNode& region_b = *crucible::make_region(test.alloc, arena, ops_b.view());
    region_a.next = crucible::make_terminal(test.alloc, arena);
    region_b.next = crucible::make_terminal(test.alloc, arena);
    crucible::BranchNode& branch =
        make_two_arm_branch(test.alloc, arena, crucible::make_link(region_a), crucible::make_link(region_b));
    branch.next = crucible::make_terminal(test.alloc, arena);

    // The doors of a region and of a branch give the node itself, and no
    // value for each other kind.
    for (const ::fixy::Ref<crucible::RegionNode>& door_region : region_a.as_region()) {
        assert(::fixy::same_object(door_region, ::fixy::to(region_a)));
    }
    for (const ::fixy::Ref<crucible::BranchNode>& door_branch : branch.as_branch()) {
        assert(::fixy::same_object(door_branch, ::fixy::to(branch)));
    }
    assert(region_a.as_region().is_some());
    assert(branch.as_branch().is_some());
    assert(region_a.as_branch().is_none());
    assert(branch.as_region().is_none());
    assert(branch.as_loop().is_none());
    assert(loop.as_region().is_none());
    assert(terminal.as_branch().is_none());

    // The arms keep their targets, and resolved_target gives the node.
    const crucible::BranchNode::Arm& first_arm = *branch.arms.get(0).expect("the branch holds two arms");
    const crucible::BranchNode::Arm& second_arm = *branch.arms.get(1).expect("the branch holds two arms");
    assert(crucible::is_link_to(first_arm.target, region_a));
    assert(crucible::is_link_to(second_arm.target, region_b));
    assert(crucible::is_link_to(crucible::make_link(first_arm.resolved_target()), region_a));

    // The merkle hash of the branch folds the merkle hash of each target in
    // the order of the arms, and then the hash of the continuation.  The
    // format is the same as when the target was a raw pointer.
    crucible::recompute_merkle(&branch);
    uint64_t expected = crucible::detail::fmix64(branch.guard.hash());
    expected ^= crucible::detail::fmix64(region_a.merkle_hash.raw());
    expected *= 0x9E3779B97F4A7C15ULL;
    expected ^= crucible::detail::fmix64(region_b.merkle_hash.raw());
    expected *= 0x9E3779B97F4A7C15ULL;
    expected = crucible::detail::fmix64(expected ^ uint64_t{0});  // the continuation is TERMINAL, of hash zero
    assert(region_a.merkle_hash.raw() != 0 && region_b.merkle_hash.raw() != 0);
    assert(branch.merkle_hash.raw() == expected);

    // An image of the branch holds the merkle hash of each target.  A
    // resolver that knows only the first target gives the second arm no
    // target, and the image of the loaded branch writes zero for it.
    ::fixy::HeapArray<std::byte> image = ::fixy::mint_heap_array(test.alloc, 4096, std::byte{0});
    const size_t written = crucible::serialize_branch(crucible::SerializedBranch{branch}, image.view());
    assert(written > 0);
    crucible::BranchNode& loaded = *crucible::deserialize_branch(
        test.alloc, image.view().window(0, written).expect("the writer writes inside its image"), arena,
        [&region_a](crucible::MerkleHash hash) noexcept {
            return hash == region_a.merkle_hash ? crucible::make_link(region_a) : crucible::TraceLink{};
        });
    const crucible::BranchNode::Arm& loaded_first = *loaded.arms.get(0).expect("the image holds two arms");
    const crucible::BranchNode::Arm& loaded_second = *loaded.arms.get(1).expect("the image holds two arms");
    assert(crucible::is_link_to(loaded_first.target, region_a));
    assert(loaded_second.target.is_none());

    ::fixy::HeapArray<std::byte> image_again = ::fixy::mint_heap_array(test.alloc, 4096, std::byte{0});
    const size_t written_again = crucible::serialize_branch(crucible::SerializedBranch{loaded}, image_again.view());
    assert(written_again == written);
    // The hash of the second target is the last eight bytes of the image.
    for (size_t index = written - 8; index < written; ++index) {
        assert(*image_again.get(index).expect("the index is below the count written") == std::byte{0});
    }

    // The loader with no resolver gives each arm no target.
    crucible::BranchNode& unresolved = *crucible::deserialize_branch(
        test.alloc, image.view().window(0, written).expect("the writer writes inside its image"), arena);
    for (const crucible::BranchNode::Arm& arm : unresolved.arms) {
        assert(arm.target.is_none());
    }

    crucible::test::pass("test_merkle_links: all tests passed\n");
    return 0;
}
