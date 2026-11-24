#pragma once

// A branch image.  The loader either refuses the bytes or returns a branch
// whose arm count is in range, whose arms are sorted by value (replay finds
// an arm by binary search, so an unsorted branch routes to the wrong
// target), and which writes back to an image that loads to the same branch.

#include "../harness.h"

#include <crucible/MerkleDag.h>
#include <crucible/Serialize.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace crucible::fuzz::boundary {

// The image of a branch whose arms hold the given values, in that order.
[[nodiscard]] inline SeedBytes branch_image(::fixy::View<std::int64_t const> values) noexcept {
    ::fixy::BumpArena<DagArena> arena =
        ::fixy::mint_bump_arena<DagArena>(::fixy::Quantity<::fixy::unit::Byte, std::size_t>{std::size_t{1} << 20});
    const auto alloc = test_alloc();
    const auto arms = static_cast<std::uint32_t>(values.size());
    const auto branch = mint_dag_node<BranchNode>(alloc, arena);
    branch->kind = TraceNodeKind::BRANCH;
    branch->merkle_hash = MerkleHash{0x5eed0000u + arms};
    auto arms_run = arena.array<BranchNode::Arm>(alloc, arms, BranchNode::Arm{});
    branch->set_arms(arms_run);
    for (std::uint32_t i = 0; i < arms; ++i)
        *branch->arms().get(i).expect("i is below the count of arms") =
            BranchNode::Arm{*values.get(i).expect("i is below the count of values"), TraceLink{}};
    ::fixy::HeapArray<std::byte> blank = ::fixy::mint_heap_array(alloc, 4096, std::byte{0});
    // The body takes the node by reference, and the serializer reads it as
    // const.
    auto const& node = *branch;
    const auto [written, image] = ::fixy::with_mut(
        std::move(blank), [&node](auto run) noexcept { return serialize_branch(::fixy::to(node), std::move(run)); });
    return copy_seed(
        ::fixy::View<std::byte const>{image.view()}.window(0, written).expect("the writer writes inside its image"));
}

// The guard kind is the first byte after the 32-byte header.
inline constexpr std::size_t kImageGuardKindOffset = 32;

[[nodiscard]] inline SeedList seeds_branch() noexcept {
    const ::fixy::FixedArray<std::int64_t, 1> one{-3};
    const ::fixy::FixedArray<std::int64_t, 3> three{-3, 4, 11};
    const ::fixy::FixedArray<std::int64_t, 2> unsorted{5, 1};
    const ::fixy::FixedArray<std::int64_t, 1> zero{0};
    SeedList seeds = empty_seed_list();
    add_seed(seeds, branch_image(::fixy::View<std::int64_t const>{}));
    add_seed(seeds, branch_image(one.view()));
    add_seed(seeds, branch_image(three.view()));

    // Regressions: images an earlier loader accepted.  Unsorted arms route
    // a guard outcome to the wrong target, and a guard kind that names no
    // kind reaches the replay switch.
    add_seed(seeds, branch_image(unsorted.view()));
    SeedBytes bad_kind = branch_image(zero.view());
    bad_kind.with_element(
        kImageGuardKindOffset, [](std::byte& kind) noexcept { kind = std::byte{0x7F}; },
        [] noexcept { ::fixy::fatal("the image holds the guard kind"); });
    add_seed(seeds, std::move(bad_kind));
    return seeds;
}

inline void run_branch(::fixy::View<std::byte const> bytes) noexcept {
    constexpr std::size_t kMaxInputBytes = std::size_t{1} << 20;
    const ::fixy::View<std::byte const> input =
        bytes.size() > kMaxInputBytes ? bytes.window(0, kMaxInputBytes).expect("the cap is inside the input") : bytes;

    ::fixy::BumpArena<DagArena> arena =
        ::fixy::mint_bump_arena<DagArena>(::fixy::Quantity<::fixy::unit::Byte, std::size_t>{std::size_t{1} << 20});
    const auto alloc = test_alloc();
    const ::fixy::Option<DagRef<BranchNode>> loaded = deserialize_branch(alloc, input, arena).ok();
    for (const DagRef<BranchNode> branch : loaded) {
        CRUCIBLE_FUZZ_CLAIM("branch", branch->num_arms() <= CDAG_MAX_BRANCH_ARMS);
        CRUCIBLE_FUZZ_CLAIM("branch", branch->are_arms_sorted_by_value());

        ::fixy::HeapArray<std::byte> blank = ::fixy::mint_heap_array(alloc, input.size() + 256, std::byte{0});
        const auto [written, image] = ::fixy::with_mut(std::move(blank), [&branch](auto run) noexcept {
            return serialize_branch(::fixy::to(*branch), std::move(run));
        });
        CRUCIBLE_FUZZ_CLAIM("branch", written > 0);
        const ::fixy::Option<DagRef<BranchNode>> reloaded =
            deserialize_branch(alloc, image.view().window(0, written).expect("the writer writes inside its image"),
                               arena)
                .ok();
        CRUCIBLE_FUZZ_CLAIM("branch", reloaded.is_some());
        for (const DagRef<BranchNode> again : reloaded) {
            CRUCIBLE_FUZZ_CLAIM("branch", again->num_arms() == branch->num_arms());
            CRUCIBLE_FUZZ_CLAIM("branch", again->merkle_hash == branch->merkle_hash);
            for (std::uint32_t i = 0; i < branch->num_arms(); ++i) {
                const BranchNode::Arm& arm = *branch->arms().get(i).expect("i is below the count of arms");
                const BranchNode::Arm& again_arm =
                    *again->arms().get(i).expect("the two branches hold one count of arms");
                CRUCIBLE_FUZZ_CLAIM("branch", again_arm.value == arm.value);
            }
        }
    }
}

}  // namespace crucible::fuzz::boundary
