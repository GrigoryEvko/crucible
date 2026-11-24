// The links of the Merkle DAG and the kind-checked doors of its nodes.

#include <crucible/MerkleDag.h>
#include <crucible/Serialize.h>
#include <fixy/Protocol.h>
#include <foundation/reflect/Mix.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

// A branch of two arms, of the values 1 and 2, in the arena.
[[nodiscard]] crucible::BranchNode& make_two_arm_branch(::foundation::effects::Alloc alloc,
                                                        ::fixy::BumpArena<crucible::DagArena>& arena,
                                                        crucible::TraceLink first,
                                                        crucible::TraceLink second) noexcept {
    crucible::BranchNode& branch = *crucible::mint_dag_node<crucible::BranchNode>(alloc, arena);
    branch.kind = crucible::TraceNodeKind::BRANCH;
    auto arms_run = arena.array<crucible::BranchNode::Arm>(alloc, 2, crucible::BranchNode::Arm{});
    branch.set_arms(arms_run);
    *branch.arms().get(0).expect("the branch holds two arms") = crucible::BranchNode::Arm{1, first};
    *branch.arms().get(1).expect("the branch holds two arms") = crucible::BranchNode::Arm{2, second};
    return branch;
}

}  // namespace

[[gnu::cold]] int main() {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<crucible::DagArena> arena = ::fixy::mint_bump_arena<crucible::DagArena>(
        ::fixy::Quantity<::fixy::unit::Byte, std::size_t>{std::size_t{1} << 16});

    auto ops_run = arena.array(test.alloc, 1, crucible::TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<crucible::TraceEntry, 1> const ops = ops_run.view().window<1>(0).expect("the run holds one op");
    ops.at(::fixy::index<0>()).schema_hash = crucible::SchemaHash{0x1111};
    crucible::RegionNode& region = *crucible::make_region(test.alloc, arena, ops_run);
    crucible::TraceNode& terminal = *crucible::make_terminal(test.alloc, arena);
    crucible::LoopNode& loop = *crucible::make_loop(
        test.alloc, arena, region, crucible::ContentHash{0x5EED},
        ::fixy::ArenaRun<crucible::FeedbackEdge const, crucible::DagArena>{}, crucible::LoopTermKind::REPEAT, 2);

    // A link takes one pointer, and a link with no value is the default.
    static_assert(sizeof(crucible::TraceLink) == sizeof(void*));
    CRUCIBLE_TEST_ASSERT(kTest, crucible::TraceLink{}.is_none());

    // make_link gives the full link to the node.  is_link_to compares the
    // address of the linked node with the address of the node.
    const crucible::TraceLink to_region = crucible::make_link(region);
    CRUCIBLE_TEST_ASSERT(kTest, to_region.is_some());
    CRUCIBLE_TEST_ASSERT(kTest, crucible::is_link_to(to_region, region));
    CRUCIBLE_TEST_ASSERT(kTest, !crucible::is_link_to(to_region, terminal));
    CRUCIBLE_TEST_ASSERT(kTest, !crucible::is_link_to(crucible::TraceLink{}, region));

    // A copy of a link reaches the same node.
    crucible::TraceLink cursor{};
    cursor = to_region;
    CRUCIBLE_TEST_ASSERT(kTest, crucible::is_link_to(cursor, region));

    // make_loop links its body.
    CRUCIBLE_TEST_ASSERT(kTest, crucible::is_link_to(loop.body.option(), region));

    // The door of a loop gives the node itself for a loop, and no value for
    // each other kind.
    CRUCIBLE_TEST_ASSERT(kTest, loop.as_loop().is_some());
    for (const crucible::DagRef<crucible::LoopNode>& door_loop : loop.as_loop()) {
        CRUCIBLE_TEST_ASSERT(kTest, ::fixy::same_object(door_loop, ::fixy::to(loop)));
    }
    CRUCIBLE_TEST_ASSERT(kTest, region.as_loop().is_none());
    CRUCIBLE_TEST_ASSERT(kTest, terminal.as_loop().is_none());

    // ── The arms of a branch ──────────────────────────────────────────
    auto ops_a_run = arena.array(test.alloc, 1, crucible::TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<crucible::TraceEntry, 1> const ops_a = ops_a_run.view().window<1>(0).expect("the run holds one op");
    ops_a.at(::fixy::index<0>()).schema_hash = crucible::SchemaHash{0xA1};
    auto ops_b_run = arena.array(test.alloc, 1, crucible::TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<crucible::TraceEntry, 1> const ops_b = ops_b_run.view().window<1>(0).expect("the run holds one op");
    ops_b.at(::fixy::index<0>()).schema_hash = crucible::SchemaHash{0xB2};
    crucible::RegionNode& region_a = *crucible::make_region(test.alloc, arena, ops_a_run);
    crucible::RegionNode& region_b = *crucible::make_region(test.alloc, arena, ops_b_run);
    region_a.next = crucible::make_link(*crucible::make_terminal(test.alloc, arena));
    region_b.next = crucible::make_link(*crucible::make_terminal(test.alloc, arena));
    crucible::BranchNode& branch =
        make_two_arm_branch(test.alloc, arena, crucible::make_link(region_a), crucible::make_link(region_b));
    branch.next = crucible::make_link(*crucible::make_terminal(test.alloc, arena));

    // The doors of a region and of a branch give the node itself, and no
    // value for each other kind.
    for (const crucible::DagRef<crucible::RegionNode>& door_region : region_a.as_region()) {
        CRUCIBLE_TEST_ASSERT(kTest, ::fixy::same_object(door_region, ::fixy::to(region_a)));
    }
    for (const crucible::DagRef<crucible::BranchNode>& door_branch : branch.as_branch()) {
        CRUCIBLE_TEST_ASSERT(kTest, ::fixy::same_object(door_branch, ::fixy::to(branch)));
    }
    CRUCIBLE_TEST_ASSERT(kTest, region_a.as_region().is_some());
    CRUCIBLE_TEST_ASSERT(kTest, branch.as_branch().is_some());
    CRUCIBLE_TEST_ASSERT(kTest, region_a.as_branch().is_none());
    CRUCIBLE_TEST_ASSERT(kTest, branch.as_region().is_none());
    CRUCIBLE_TEST_ASSERT(kTest, branch.as_loop().is_none());
    CRUCIBLE_TEST_ASSERT(kTest, loop.as_region().is_none());
    CRUCIBLE_TEST_ASSERT(kTest, terminal.as_branch().is_none());

    // The doors of a walk that knows the kind give the node itself.
    // test_merkle_dag holds the refusal of a node of a different kind.
    CRUCIBLE_TEST_ASSERT(kTest, ::fixy::same_object(region_a.expect_region(), ::fixy::to(region_a)));
    CRUCIBLE_TEST_ASSERT(kTest, ::fixy::same_object(branch.expect_branch(), ::fixy::to(branch)));
    CRUCIBLE_TEST_ASSERT(kTest, ::fixy::same_object(loop.expect_loop(), ::fixy::to(loop)));

    // The arms keep their targets, and resolved_target gives the node.
    crucible::BranchNode::Arm& first_arm = *branch.arms().get(0).expect("the branch holds two arms");
    const crucible::BranchNode::Arm& second_arm = *branch.arms().get(1).expect("the branch holds two arms");
    CRUCIBLE_TEST_ASSERT(kTest, crucible::is_link_to(first_arm.target(), region_a));
    CRUCIBLE_TEST_ASSERT(kTest, crucible::is_link_to(second_arm.target(), region_b));
    CRUCIBLE_TEST_ASSERT(kTest, crucible::is_link_to(crucible::make_link(first_arm.resolved_target()), region_a));

    // The merkle hash of the branch folds the merkle hash of each target in
    // the order of the arms, and then the hash of the continuation.  The
    // format is the same as when the target was a raw pointer.
    crucible::recompute_merkle(branch);
    uint64_t expected = ::foundation::reflect::fmix64(::fixy::hash(branch.guard).raw());
    expected ^= ::foundation::reflect::fmix64(region_a.merkle_hash.raw());
    expected *= 0x9E3779B97F4A7C15ULL;
    expected ^= ::foundation::reflect::fmix64(region_b.merkle_hash.raw());
    expected *= 0x9E3779B97F4A7C15ULL;
    expected = ::foundation::reflect::fmix64(expected ^ uint64_t{0});  // the continuation is TERMINAL, of hash zero
    CRUCIBLE_TEST_ASSERT(kTest, region_a.merkle_hash.raw() != 0 && region_b.merkle_hash.raw() != 0);
    CRUCIBLE_TEST_ASSERT(kTest, branch.merkle_hash.raw() == expected);
    // The store keeps the merkle hash of a branch, so the value of this
    // branch is frozen.  It folds the hash of the default guard, and
    // CDAG_VERSION 18 pins it.
    CRUCIBLE_TEST_ASSERT(kTest, branch.merkle_hash.raw() == 0xCC66375416F9F4E9ULL);

    // An image of the branch holds the merkle hash of each target.  A
    // resolver that knows only the first target gives the second arm no
    // target, and the image of the loaded branch writes zero for it.
    auto const& branch_read = branch;
    auto [written, image] =
        ::fixy::with_mut(::fixy::mint_heap_array(test.alloc, 4096, std::byte{0}), [&branch_read](auto run) noexcept {
            return crucible::serialize_branch(::fixy::to(branch_read), std::move(run));
        });
    CRUCIBLE_TEST_ASSERT(kTest, written > 0);
    crucible::BranchNode& loaded =
        *crucible::deserialize_branch(
             test.alloc, image.view().window(0, written).expect("the writer writes inside its image"), arena,
             [&region_a](crucible::MerkleHash hash) noexcept {
                 return hash == region_a.merkle_hash ? crucible::make_link(region_a) : crucible::TraceLink{};
             })
             .expect("the image of a branch loads");
    const crucible::BranchNode::Arm& loaded_first = *loaded.arms().get(0).expect("the image holds two arms");
    const crucible::BranchNode::Arm& loaded_second = *loaded.arms().get(1).expect("the image holds two arms");
    CRUCIBLE_TEST_ASSERT(kTest, crucible::is_link_to(loaded_first.target(), region_a));
    CRUCIBLE_TEST_ASSERT(kTest, loaded_second.target().is_none());

    auto const& loaded_read = loaded;
    auto [written_again, image_again] =
        ::fixy::with_mut(::fixy::mint_heap_array(test.alloc, 4096, std::byte{0}), [&loaded_read](auto run) noexcept {
            return crucible::serialize_branch(::fixy::to(loaded_read), std::move(run));
        });
    CRUCIBLE_TEST_ASSERT(kTest, written_again == written);
    // The hash of the second target is the last eight bytes of the image.
    for (size_t index = written - 8; index < written; ++index) {
        CRUCIBLE_TEST_ASSERT(kTest,
                             *image_again.get(index).expect("the index is below the count written") == std::byte{0});
    }

    // The loader with no resolver gives each arm no target.
    crucible::BranchNode& unresolved =
        *crucible::deserialize_branch(
             test.alloc, image.view().window(0, written).expect("the writer writes inside its image"), arena)
             .expect("the image of a branch loads");
    for (const crucible::BranchNode::Arm& arm : unresolved.arms()) {
        CRUCIBLE_TEST_ASSERT(kTest, arm.target().is_none());
    }

    // ── The walks along the links ────────────────────────────────────
    // replay runs the arm of the guard value, and then the continuation.
    // An unseen value, and an arm with no target, stop the replay.
    auto replay_with = [&](crucible::TraceNode& first, int64_t guard_value, uint32_t& ran_a, uint32_t& ran_b) {
        return crucible::replay(
            first, [guard_value](const crucible::Guard&) noexcept { return guard_value; },
            [&](crucible::RegionNode const& ran) noexcept {
                if (::fixy::same_object(::fixy::to(ran), ::fixy::to(region_a))) ++ran_a;
                if (::fixy::same_object(::fixy::to(ran), ::fixy::to(region_b))) ++ran_b;
            });
    };
    uint32_t ran_a = 0;
    uint32_t ran_b = 0;
    CRUCIBLE_TEST_ASSERT(kTest, replay_with(branch, 2, ran_a, ran_b));
    CRUCIBLE_TEST_ASSERT(kTest, ran_a == 0 && ran_b == 1);
    CRUCIBLE_TEST_ASSERT(kTest, replay_with(branch, 1, ran_a, ran_b));
    CRUCIBLE_TEST_ASSERT(kTest, ran_a == 1 && ran_b == 1);
    CRUCIBLE_TEST_ASSERT(kTest, !replay_with(branch, 3, ran_a, ran_b));
    CRUCIBLE_TEST_ASSERT(kTest, !replay_with(loaded, 2, ran_a, ran_b));
    CRUCIBLE_TEST_ASSERT(kTest, ran_a == 1 && ran_b == 1);

    // The count of nodes: the branch, the two arm regions with their
    // TERMINAL nodes, and the TERMINAL continuation.  An empty link has no
    // node.
    CRUCIBLE_TEST_ASSERT(kTest, crucible::dag_node_count(crucible::make_link(branch)) == 6);
    CRUCIBLE_TEST_ASSERT(kTest, crucible::dag_node_count(crucible::TraceLink{}) == 0);

    // collect_regions reads the region of each arm.
    ::foundation::core::FixedArray<crucible::RegionNode const*, 4> collected{};
    CRUCIBLE_TEST_ASSERT(kTest, collected.with_mut([&branch_read](auto run) noexcept {
        return crucible::collect_regions(branch_read, run);
    }) == 2);
    CRUCIBLE_TEST_ASSERT(kTest, crucible::compute_merkle_hash(branch) == branch.merkle_hash);

    // dag_diff agrees on one DAG and on two empty links, and reports an end
    // of one chain against a node of the other.
    CRUCIBLE_TEST_ASSERT(kTest, crucible::dag_diff(crucible::make_link(branch), crucible::make_link(branch)).kind
                                    == crucible::DagDiff::Kind::IDENTICAL);
    CRUCIBLE_TEST_ASSERT(kTest, crucible::dag_diff(crucible::TraceLink{}, crucible::TraceLink{}).kind
                                    == crucible::DagDiff::Kind::IDENTICAL);
    const crucible::DagDiff ended = crucible::dag_diff(crucible::make_link(region), crucible::TraceLink{});
    CRUCIBLE_TEST_ASSERT(kTest, ended.kind == crucible::DagDiff::Kind::STRUCTURE_MISMATCH);
    CRUCIBLE_TEST_ASSERT(kTest, crucible::is_link_to(ended.node_a, region));
    CRUCIBLE_TEST_ASSERT(kTest, ended.node_b.is_none());

    // find_merge_point gives the link to the first region of the shared
    // tail, and an empty link when the tails differ.
    const ::fixy::View<const crucible::TraceEntry> tail_ops = ops_b;
    CRUCIBLE_TEST_ASSERT(
        kTest, crucible::is_link_to(crucible::find_merge_point(tail_ops, crucible::make_link(region_b), ::fixy::none),
                                    region_b));
    CRUCIBLE_TEST_ASSERT(kTest,
                         crucible::find_merge_point(tail_ops, crucible::make_link(region_a), ::fixy::none).is_none());
    CRUCIBLE_TEST_ASSERT(kTest, crucible::find_merge_point(tail_ops, crucible::TraceLink{}, ::fixy::none).is_none());

    ::fixy::check::pass(kTest, "test_merkle_links: all tests passed\n");
    return 0;
}
