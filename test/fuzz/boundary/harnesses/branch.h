#pragma once

// A branch image.  The loader either refuses the bytes or returns a branch
// whose arm count is in range, whose arms are sorted by value (replay finds
// an arm by binary search, so an unsorted branch routes to the wrong
// target), and which writes back to an image that loads to the same branch.

#include "../harness.h"

#include <crucible/MerkleDag.h>
#include <crucible/Serialize.h>
#include <foundation/Quarantine.h>
#include <foundation/core/Region.h>

#include <array>
#include <vector>

namespace crucible::fuzz::boundary {

// The image of a branch whose arms hold the given values, in that order.
[[nodiscard]] inline std::vector<std::uint8_t> branch_image(std::span<const std::int64_t> values) {
    ::fixy::BumpArena arena{};
    const auto alloc = test_alloc();
    const auto arms = static_cast<std::uint32_t>(values.size());
    BranchNode* branch = &*arena.make<BranchNode>(alloc);
    branch->kind = TraceNodeKind::BRANCH;
    branch->merkle_hash = MerkleHash{0x5eed0000u + arms};
    branch->arms = arena.array<BranchNode::Arm>(alloc, arms, BranchNode::Arm{});
    for (std::uint32_t i = 0; i < arms; ++i)
        *branch->arms.get(i).expect("i is below the count of arms") = BranchNode::Arm{.value = values[i]};
    ::fixy::HeapArray<std::byte> image = ::fixy::mint_heap_array(alloc, 4096, std::byte{0});
    const size_t written = serialize_branch(SerializedBranch{*branch}, image.view());
    std::vector<std::uint8_t> seed(written);
    for (size_t index = 0; index < written; ++index) {
        seed[index] = static_cast<std::uint8_t>(*image.get(index).expect("the writer writes inside its image"));
    }
    return seed;
}

// The guard kind is the first byte after the 32-byte header.
inline constexpr std::size_t kImageGuardKindOffset = 32;

[[nodiscard]] inline Seeds seeds_branch() {
    Seeds seeds;
    seeds.push_back(branch_image({}));
    seeds.push_back(branch_image(std::array<std::int64_t, 1>{-3}));
    seeds.push_back(branch_image(std::array<std::int64_t, 3>{-3, 4, 11}));

    // Regressions: images an earlier loader accepted.  Unsorted arms route
    // a guard outcome to the wrong target, and a guard kind that names no
    // kind reaches the replay switch.
    seeds.push_back(branch_image(std::array<std::int64_t, 2>{5, 1}));
    auto bad_kind = branch_image(std::array<std::int64_t, 1>{0});
    bad_kind[kImageGuardKindOffset] = 0x7F;
    seeds.push_back(std::move(bad_kind));
    return seeds;
}

inline void run_branch(std::span<const std::uint8_t> bytes) {
    constexpr std::size_t kMaxInputBytes = std::size_t{1} << 20;
    if (bytes.size() > kMaxInputBytes) bytes = bytes.first(kMaxInputBytes);

    ::fixy::BumpArena arena{};
    const auto alloc = test_alloc();
    // The input lives for the call of the harness, so its View is valid
    // here.
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the fuzzer gives its input as a pointer to raw bytes and a count")
    const ::fixy::View<const std::byte> input =
        ::foundation::core::byte_view(::fixy::mint_foreign_view(bytes.data(), bytes.size()));
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    const ::fixy::Option<::fixy::Ref<BranchNode>> loaded = deserialize_branch(alloc, input, arena).ok();
    for (const ::fixy::Ref<BranchNode> branch : loaded) {
        CRUCIBLE_FUZZ_CLAIM("branch", branch->num_arms() <= CDAG_MAX_BRANCH_ARMS);
        CRUCIBLE_FUZZ_CLAIM("branch", branch->are_arms_sorted_by_value());

        ::fixy::HeapArray<std::byte> image = ::fixy::mint_heap_array(alloc, input.size() + 256, std::byte{0});
        const size_t written = serialize_branch(SerializedBranch{*branch}, image.view());
        CRUCIBLE_FUZZ_CLAIM("branch", written > 0);
        const ::fixy::Option<::fixy::Ref<BranchNode>> reloaded =
            deserialize_branch(alloc, image.view().window(0, written).expect("the writer writes inside its image"),
                               arena)
                .ok();
        CRUCIBLE_FUZZ_CLAIM("branch", reloaded.is_some());
        for (const ::fixy::Ref<BranchNode> again : reloaded) {
            CRUCIBLE_FUZZ_CLAIM("branch", again->num_arms() == branch->num_arms());
            CRUCIBLE_FUZZ_CLAIM("branch", again->merkle_hash == branch->merkle_hash);
            for (std::uint32_t i = 0; i < branch->num_arms(); ++i) {
                const BranchNode::Arm& arm = *branch->arms.get(i).expect("i is below the count of arms");
                const BranchNode::Arm& again_arm =
                    *again->arms.get(i).expect("the two branches hold one count of arms");
                CRUCIBLE_FUZZ_CLAIM("branch", again_arm.value == arm.value);
            }
        }
    }
}

}  // namespace crucible::fuzz::boundary
