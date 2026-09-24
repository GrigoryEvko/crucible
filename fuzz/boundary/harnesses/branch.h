#pragma once

// A branch image.  The loader either refuses the bytes or returns a branch
// whose arm count is in range, whose arms are sorted by value (replay finds
// an arm by binary search, so an unsorted branch routes to the wrong
// target), and which writes back to an image that loads to the same branch.

#include "../harness.h"
#include "../old_tree.h"

#include <crucible/Arena.h>
#include <crucible/MerkleDag.h>
#include <crucible/Serialize.h>

#include <array>
#include <vector>

namespace crucible::fuzz::boundary {

// The image of a branch whose arms hold the given values, in that order.
[[nodiscard]] inline std::vector<std::uint8_t> branch_image(std::span<const std::int64_t> values) {
    Arena arena;
    const auto alloc = old_tree::test_alloc();
    const auto arms = static_cast<std::uint32_t>(values.size());
    auto* branch = ::new (arena.alloc_obj<BranchNode>(alloc)) BranchNode{};
    branch->kind = TraceNodeKind::BRANCH;
    branch->merkle_hash = MerkleHash{0x5eed0000u + arms};
    branch->num_arms = arms;
    branch->arms = arms == 0 ? nullptr : arena.alloc_array<BranchNode::Arm>(alloc, arms);
    for (std::uint32_t i = 0; i < arms; ++i) branch->arms[i] = BranchNode::Arm{.value = values[i]};
    std::vector<std::uint8_t> image(4096);
    image.resize(serialize_branch(branch, std::span<std::uint8_t>{image}));
    return image;
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

    Arena arena;
    const auto alloc = old_tree::test_alloc();
    BranchNode* branch = deserialize_branch(alloc, bytes, arena, nullptr);
    if (branch == nullptr) return;
    CRUCIBLE_FUZZ_CLAIM("branch", branch->num_arms <= CDAG_MAX_BRANCH_ARMS);
    CRUCIBLE_FUZZ_CLAIM("branch", branch->num_arms == 0 || branch->arms != nullptr);
    CRUCIBLE_FUZZ_CLAIM("branch", branch->are_arms_sorted_by_value());

    std::vector<std::uint8_t> image(bytes.size() + 256);
    const std::size_t written = serialize_branch(branch, std::span<std::uint8_t>{image});
    CRUCIBLE_FUZZ_CLAIM("branch", written > 0);
    BranchNode* again = deserialize_branch(alloc, std::span<const std::uint8_t>{image.data(), written}, arena, nullptr);
    CRUCIBLE_FUZZ_CLAIM("branch", again != nullptr);
    CRUCIBLE_FUZZ_CLAIM("branch", again->num_arms == branch->num_arms);
    CRUCIBLE_FUZZ_CLAIM("branch", again->merkle_hash == branch->merkle_hash);
    for (std::uint32_t i = 0; i < branch->num_arms; ++i) {
        CRUCIBLE_FUZZ_CLAIM("branch", again->arms[i].value == branch->arms[i].value);
    }
}

}  // namespace crucible::fuzz::boundary
