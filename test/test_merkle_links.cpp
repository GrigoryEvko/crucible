// The links of the Merkle DAG, the kind-checked doors of its nodes, and the
// branch that add_branch builds at a point of divergence.

#include <crucible/KernelCache.h>
#include <crucible/MerkleDag.h>
#include <crucible/Serialize.h>
#include <fixy/Protocol.h>
#include <foundation/reflect/Mix.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/core/ArenaFrame.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

// A branch of two arms, of the values 1 and 2, in the arena of the frame.  The
// arm of the value 1 runs first, and the arm of the value 2 runs second.
template <class Fresh>
[[nodiscard]] crucible::DagFrameRef<crucible::BranchNode, Fresh>
make_two_arm_branch(::foundation::effects::Alloc alloc, crucible::DagFrame<Fresh>& frame,
                    std::type_identity_t<crucible::DagFrameRef<crucible::TraceNode, Fresh>> first,
                    std::type_identity_t<crucible::DagFrameRef<crucible::TraceNode, Fresh>> second) noexcept {
    auto const branch = crucible::mint_dag_node<crucible::BranchNode>(alloc, frame);
    branch->kind = crucible::TraceNodeKind::BRANCH;
    auto arms = frame.template build_array<crucible::BranchNode::Arm>(
        alloc, ::fixy::ElementCount{2},
        [](::fixy::RunStart at) noexcept { return crucible::BranchNode::Arm{at.value().raw() == 0 ? 1 : 2}; });
    frame.store(arms.element(::fixy::RunStart{::fixy::ElementCount{0}}).expect("the branch holds two arms"),
                &crucible::BranchNode::Arm::target, first);
    frame.store(arms.element(::fixy::RunStart{::fixy::ElementCount{1}}).expect("the branch holds two arms"),
                &crucible::BranchNode::Arm::target, second);
    frame.store(branch, &crucible::BranchNode::arm_run, arms);
    return branch;
}

// The checks of the links, the doors, the arms, the walks and the image of a
// branch, each node in the arena of the frame.
template <class Fresh>
void check_links(::foundation::effects::Alloc alloc, crucible::DagFrame<Fresh>& frame) noexcept {
    auto ops_run = frame.array(alloc, std::size_t{1}, crucible::TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<crucible::TraceEntry, 1> const ops =
        ops_run.view().template window<1>(0).expect("the run holds one op");
    ops.at(::fixy::index<0>()).schema_hash = crucible::SchemaHash{0x1111};
    auto const region_node = crucible::make_region(alloc, frame, ops_run);
    crucible::RegionNode& region = *region_node;
    auto const terminal_node = crucible::make_terminal(alloc, frame);
    crucible::TraceNode& terminal = *terminal_node;
    auto no_edges_run = frame.array(alloc, std::size_t{0}, crucible::FeedbackEdge{});
    auto const loop_node = crucible::make_loop(alloc, frame, region_node, crucible::ContentHash{0x5EED}, no_edges_run,
                                               crucible::LoopTermKind::REPEAT, 2);
    crucible::LoopNode& loop = *loop_node;

    // A FrameRef of a node of the frame, and the upcast of a region to it,
    // which names the same object.
    using TraceRef = std::remove_const_t<decltype(terminal_node)>;
    TraceRef const region_as_trace = region_node;
    crucible::TraceNode& region_base = region;
    CRUCIBLE_TEST_ASSERT(kTest, ::fixy::same_object(region_as_trace.weaken(), ::fixy::to(region_base)));
    CRUCIBLE_TEST_ASSERT(kTest, &*region_as_trace == &region_base);

    // A link takes one pointer, and a link with no value is the default.
    static_assert(sizeof(crucible::TraceLink) == sizeof(void*));
    CRUCIBLE_TEST_ASSERT(kTest, crucible::TraceLink{}.is_none());

    // A link holds the family Ref of the node.  is_link_to compares the
    // address of the linked node with the address of the node.
    const crucible::TraceLink to_region = crucible::TraceLink::some(region_as_trace.weaken());
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
    auto ops_a_run = frame.array(alloc, std::size_t{1}, crucible::TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<crucible::TraceEntry, 1> const ops_a =
        ops_a_run.view().template window<1>(0).expect("the run holds one op");
    ops_a.at(::fixy::index<0>()).schema_hash = crucible::SchemaHash{0xA1};
    auto ops_b_run = frame.array(alloc, std::size_t{1}, crucible::TraceEntry{});
    // A plain View of an arena run: the View carries no brand of its arena.
    ::fixy::View<crucible::TraceEntry, 1> const ops_b =
        ops_b_run.view().template window<1>(0).expect("the run holds one op");
    ops_b.at(::fixy::index<0>()).schema_hash = crucible::SchemaHash{0xB2};
    auto const region_a_node = crucible::make_region(alloc, frame, ops_a_run);
    crucible::RegionNode& region_a = *region_a_node;
    auto const region_b_node = crucible::make_region(alloc, frame, ops_b_run);
    crucible::RegionNode& region_b = *region_b_node;
    auto const region_a_end = crucible::make_terminal(alloc, frame);
    frame.store(region_a_node, &crucible::TraceNode::next, region_a_end);
    auto const region_b_end = crucible::make_terminal(alloc, frame);
    frame.store(region_b_node, &crucible::TraceNode::next, region_b_end);
    auto const branch_node = make_two_arm_branch(alloc, frame, region_a_node, region_b_node);
    crucible::BranchNode& branch = *branch_node;
    auto const branch_end = crucible::make_terminal(alloc, frame);
    frame.store(branch_node, &crucible::TraceNode::next, branch_end);

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
    CRUCIBLE_TEST_ASSERT(kTest, crucible::is_link_to(first_arm.target.option(), region_a));
    CRUCIBLE_TEST_ASSERT(kTest, crucible::is_link_to(second_arm.target.option(), region_b));
    CRUCIBLE_TEST_ASSERT(kTest, &first_arm.resolved_target() == &static_cast<crucible::TraceNode&>(region_a));

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
        ::fixy::with_mut(::fixy::mint_heap_array(alloc, 4096, std::byte{0}), [&branch_read](auto run) noexcept {
            return crucible::serialize_branch(::fixy::to(branch_read), std::move(run));
        });
    CRUCIBLE_TEST_ASSERT(kTest, written > 0);
    TraceRef const region_a_as_trace = region_a_node;
    crucible::MerkleHash const region_a_hash = region_a.merkle_hash;
    auto const loaded_node =
        crucible::deserialize_branch(
            alloc, image.view().window(0, written).expect("the writer writes inside its image"), frame,
            [region_a_as_trace, region_a_hash](crucible::MerkleHash hash) noexcept {
                return hash == region_a_hash ? ::fixy::Option<TraceRef>::some(region_a_as_trace)
                                             : ::fixy::Option<TraceRef>{};
            })
            .expect("the image of a branch loads");
    crucible::BranchNode& loaded = *loaded_node;
    const crucible::BranchNode::Arm& loaded_first = *loaded.arms().get(0).expect("the image holds two arms");
    const crucible::BranchNode::Arm& loaded_second = *loaded.arms().get(1).expect("the image holds two arms");
    CRUCIBLE_TEST_ASSERT(kTest, crucible::is_link_to(loaded_first.target.option(), region_a));
    CRUCIBLE_TEST_ASSERT(kTest, loaded_second.target.is_none());

    auto const& loaded_read = loaded;
    auto [written_again, image_again] =
        ::fixy::with_mut(::fixy::mint_heap_array(alloc, 4096, std::byte{0}), [&loaded_read](auto run) noexcept {
            return crucible::serialize_branch(::fixy::to(loaded_read), std::move(run));
        });
    CRUCIBLE_TEST_ASSERT(kTest, written_again == written);
    // The hash of the second target is the last eight bytes of the image.
    for (size_t index = written - 8; index < written; ++index) {
        CRUCIBLE_TEST_ASSERT(kTest,
                             *image_again.get(index).expect("the index is below the count written") == std::byte{0});
    }

    // The loader with no resolver gives each arm no target.
    auto const unresolved_node =
        crucible::deserialize_branch(
            alloc, image.view().window(0, written).expect("the writer writes inside its image"), frame)
            .expect("the image of a branch loads");
    for (const crucible::BranchNode::Arm& arm : unresolved_node->arms()) {
        CRUCIBLE_TEST_ASSERT(kTest, arm.target.is_none());
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
    crucible::TraceLink const branch_link = crucible::TraceLink::some(TraceRef{branch_node}.weaken());
    CRUCIBLE_TEST_ASSERT(kTest, crucible::dag_node_count(branch_link) == 6);
    CRUCIBLE_TEST_ASSERT(kTest, crucible::dag_node_count(crucible::TraceLink{}) == 0);

    // collect_regions reads the region of each arm.
    auto const [collected_count, collected] = ::foundation::core::build_fixed_array<crucible::RegionNode const*, 4>(
        [&branch_read](auto run) noexcept { return crucible::collect_regions(branch_read, run); });
    CRUCIBLE_TEST_ASSERT(kTest, collected_count == 2);
    CRUCIBLE_TEST_ASSERT(kTest, crucible::compute_merkle_hash(branch) == branch.merkle_hash);

    // dag_diff agrees on one DAG and on two empty links, and reports an end
    // of one chain against a node of the other.
    CRUCIBLE_TEST_ASSERT(kTest,
                         crucible::dag_diff(branch_link, branch_link).kind == crucible::DagDiff::Kind::IDENTICAL);
    CRUCIBLE_TEST_ASSERT(kTest, crucible::dag_diff(crucible::TraceLink{}, crucible::TraceLink{}).kind
                                    == crucible::DagDiff::Kind::IDENTICAL);
    const crucible::DagDiff ended = crucible::dag_diff(to_region, crucible::TraceLink{});
    CRUCIBLE_TEST_ASSERT(kTest, ended.kind == crucible::DagDiff::Kind::STRUCTURE_MISMATCH);
    CRUCIBLE_TEST_ASSERT(kTest, crucible::is_link_to(ended.node_a, region));
    CRUCIBLE_TEST_ASSERT(kTest, ended.node_b.is_none());

    // find_merge_point gives the link to the first region of the shared
    // tail, and an empty link when the tails differ.
    const ::fixy::View<const crucible::TraceEntry> tail_ops = ops_b;
    crucible::TraceLink const link_b = crucible::TraceLink::some(TraceRef{region_b_node}.weaken());
    crucible::TraceLink const link_a = crucible::TraceLink::some(TraceRef{region_a_node}.weaken());
    CRUCIBLE_TEST_ASSERT(kTest,
                         crucible::is_link_to(crucible::find_merge_point(tail_ops, link_b, ::fixy::none), region_b));
    CRUCIBLE_TEST_ASSERT(kTest, crucible::find_merge_point(tail_ops, link_a, ::fixy::none).is_none());
    CRUCIBLE_TEST_ASSERT(kTest, crucible::find_merge_point(tail_ops, crucible::TraceLink{}, ::fixy::none).is_none());
}

// add_branch makes a region of the new ops, and a branch of two arms at the
// point of divergence: the old node at the lower guard value and the new
// region at the higher one.  The new region and the branch continue at the
// first region of the suffix whose ops are the tail of the new ops.  With no
// suffix, the two continue nowhere.
template <class Fresh>
void check_add_branch(::foundation::effects::Alloc alloc, ::foundation::effects::cap::Nondet nondet,
                      crucible::DagFrame<Fresh>& frame) noexcept {
    crucible::KernelCache cache{alloc};
    auto const divergence = crucible::make_terminal(alloc, frame);
    using TraceRef = std::remove_const_t<decltype(divergence)>;

    // The suffix: one region of the op 0xB2, then the end.
    auto suffix_ops_run = frame.array(alloc, std::size_t{1}, crucible::TraceEntry{});
    suffix_ops_run.view().get(0).expect("the run holds one op")->schema_hash = crucible::SchemaHash{0xB2};
    auto const suffix_region = crucible::make_region(alloc, frame, suffix_ops_run);
    auto const suffix_end = crucible::make_terminal(alloc, frame);
    frame.store(suffix_region, &crucible::TraceNode::next, suffix_end);
    TraceRef const suffix = suffix_region;

    // The new ops end with the op of the suffix.
    auto new_ops_run = frame.array(alloc, std::size_t{1}, crucible::TraceEntry{});
    new_ops_run.view().get(0).expect("the run holds one op")->schema_hash = crucible::SchemaHash{0xB2};
    auto const branch = crucible::add_branch(alloc, nondet, frame, cache, divergence, new_ops_run, 7, 3,
                                             crucible::Guard{}, ::fixy::Option<TraceRef>::some(suffix), ::fixy::none);
    CRUCIBLE_TEST_ASSERT(kTest, branch->kind == crucible::TraceNodeKind::BRANCH);
    CRUCIBLE_TEST_ASSERT(kTest, branch->num_arms() == 2u);
    CRUCIBLE_TEST_ASSERT(kTest, branch->are_arms_sorted_by_value());
    crucible::BranchNode::Arm const& low = *branch->arms().get(0).expect("the branch holds two arms");
    crucible::BranchNode::Arm const& high = *branch->arms().get(1).expect("the branch holds two arms");
    CRUCIBLE_TEST_ASSERT(kTest, low.value == 3 && high.value == 7);
    CRUCIBLE_TEST_ASSERT(kTest, crucible::is_link_to(high.target.option(), *divergence));
    crucible::TraceNode const& new_node = low.resolved_target();
    CRUCIBLE_TEST_ASSERT(kTest, new_node.kind == crucible::TraceNodeKind::REGION);
    CRUCIBLE_TEST_ASSERT(kTest, new_node.expect_region()->content_hash == suffix_region->content_hash);
    CRUCIBLE_TEST_ASSERT(kTest, crucible::is_link_to(new_node.next.option(), *suffix_region));
    CRUCIBLE_TEST_ASSERT(kTest, crucible::is_link_to(branch->next.option(), *suffix_region));
    CRUCIBLE_TEST_ASSERT(kTest, branch->merkle_hash.raw() != 0);

    // A suffix that shares no tail with the new ops gives no merge point.
    auto other_ops_run = frame.array(alloc, std::size_t{1}, crucible::TraceEntry{});
    other_ops_run.view().get(0).expect("the run holds one op")->schema_hash = crucible::SchemaHash{0xC3};
    auto const apart = crucible::add_branch(alloc, nondet, frame, cache, divergence, other_ops_run, 1, 2,
                                            crucible::Guard{}, ::fixy::Option<TraceRef>::some(suffix), ::fixy::none);
    CRUCIBLE_TEST_ASSERT(kTest, apart->next.is_none());
    CRUCIBLE_TEST_ASSERT(
        kTest,
        crucible::is_link_to(apart->arms().get(0).expect("the branch holds two arms")->target.option(), *divergence));

    // No suffix: the branch continues nowhere.
    auto const alone = crucible::add_branch(alloc, nondet, frame, cache, divergence, new_ops_run, 1, 2,
                                            crucible::Guard{}, ::fixy::none, ::fixy::none);
    CRUCIBLE_TEST_ASSERT(kTest, alone->next.is_none());

    // A suffix whose shared tail starts at its second region: the region of
    // the op 0xD4, then the region of the op 0xB2.  The new ops end with 0xB2
    // and not with 0xD4, so the walk follows one link to the merge point.
    auto head_ops_run = frame.array(alloc, std::size_t{1}, crucible::TraceEntry{});
    head_ops_run.view().get(0).expect("the run holds one op")->schema_hash = crucible::SchemaHash{0xD4};
    auto const head_region = crucible::make_region(alloc, frame, head_ops_run);
    frame.store(head_region, &crucible::TraceNode::next, suffix_region);
    auto deep_ops_run = frame.array(alloc, std::size_t{2}, crucible::TraceEntry{});
    deep_ops_run.view().get(0).expect("the run holds two ops")->schema_hash = crucible::SchemaHash{0xE5};
    deep_ops_run.view().get(1).expect("the run holds two ops")->schema_hash = crucible::SchemaHash{0xB2};
    TraceRef const head = head_region;
    auto const deep = crucible::add_branch(alloc, nondet, frame, cache, divergence, deep_ops_run, 1, 2,
                                           crucible::Guard{}, ::fixy::Option<TraceRef>::some(head), ::fixy::none);
    CRUCIBLE_TEST_ASSERT(kTest, crucible::is_link_to(deep->next.option(), *suffix_region));
    crucible::TraceNode const& deep_region = deep->arms().get(1).expect("the branch holds two arms")->resolved_target();
    CRUCIBLE_TEST_ASSERT(kTest, crucible::is_link_to(deep_region.next.option(), *suffix_region));
}

}  // namespace

[[gnu::cold]] int main() {
    auto test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<crucible::DagArena> arena = ::fixy::mint_bump_arena<crucible::DagArena>(
        ::fixy::Quantity<::fixy::unit::Byte, std::size_t>{std::size_t{1} << 16});
    ::foundation::core::with_arena_frame(arena,
                                         [alloc = test.alloc](auto& frame) noexcept { check_links(alloc, frame); });
    ::foundation::core::with_arena_frame(arena, [alloc = test.alloc, nondet = test.nondet](auto& frame) noexcept {
        check_add_branch(alloc, nondet, frame);
    });

    ::fixy::check::pass(kTest, "test_merkle_links: all tests passed\n");
    return 0;
}
