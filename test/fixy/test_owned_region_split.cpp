// Splits of an owned region into shards: the chunk arithmetic for an
// exact, an uneven and a short region, and the rebuild of the whole
// through recombine.

#include "owned_region.h"

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

namespace test_owned_region {

void test_split_into_chunk_math(TestRunnerCtx const& test) {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    using Brand = decltype(perm)::brand_type;
    auto region = OwnedRegion<std::uint64_t, DataA, Brand>::adopt(test_alloc_token(), arena, 1000, std::move(perm));

    // Each element holds its own index, so a shard's contents identify
    // the offset it was cut from.
    auto seeded = ::fixy::with_mut(std::move(region), [](auto run) noexcept {
        std::uint64_t position = 0;
        for (std::uint64_t& element : run)
            element = position++;
    });

    auto parts = ::fixy::mint_split<8>(std::move(seeded));
    auto& [s0, s1, s2, s3, s4, s5, s6, s7] = parts.shards;

    // 1000 over 8 divides exactly, so every shard is the same size.
    CRUCIBLE_TEST_ASSERT(test, s0.size() == 125);
    CRUCIBLE_TEST_ASSERT(test, s1.size() == 125);
    CRUCIBLE_TEST_ASSERT(test, s7.size() == 125);

    // The shards are views into one buffer, not copies, which is what
    // reading the seeded indices back at the right offsets shows.
    CRUCIBLE_TEST_ASSERT(test, s0.cspan()[0] == 0);
    CRUCIBLE_TEST_ASSERT(test, s0.cspan()[124] == 124);
    CRUCIBLE_TEST_ASSERT(test, s1.cspan()[0] == 125);
    CRUCIBLE_TEST_ASSERT(test, s7.cspan()[0] == 875);
    CRUCIBLE_TEST_ASSERT(test, s7.cspan()[124] == 999);

    // Each shard carries its own slice index in its tag.
    static_assert(std::remove_cvref_t<decltype(s3)>::tag_type::index == 3);
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(s3)>::tag_type::parent_type, DataA>);
}

void test_split_uneven(TestRunnerCtx const& test) {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    using Brand = decltype(perm)::brand_type;
    auto region = OwnedRegion<std::uint64_t, DataA, Brand>::adopt(test_alloc_token(), arena, 1001, std::move(perm));
    ::foundation::core::View<std::uint64_t const> const all = ::fixy::mint_borrowed(region);

    auto parts = ::fixy::mint_split<8>(std::move(region));
    auto& [s0, s1, s2, s3, s4, s5, s6, s7] = parts.shards;

    // 1001 over 8 leaves a remainder of one, so the first shard takes one
    // element more than the 125 that each other shard takes.
    CRUCIBLE_TEST_ASSERT(test, s0.size() == 126);
    CRUCIBLE_TEST_ASSERT(test, s1.size() == 125);
    CRUCIBLE_TEST_ASSERT(test, s6.size() == 125);
    CRUCIBLE_TEST_ASSERT(test, s7.size() == 125);
    CRUCIBLE_TEST_ASSERT(test, starts_at(all, s0, 0));
    CRUCIBLE_TEST_ASSERT(test, starts_at(all, s1, 126));
    CRUCIBLE_TEST_ASSERT(test, starts_at(all, s7, 1001 - 125));
}

void test_split_smaller_than_n(TestRunnerCtx const& test) {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    using Brand = decltype(perm)::brand_type;
    auto region = OwnedRegion<std::uint64_t, DataA, Brand>::adopt(test_alloc_token(), arena, 5, std::move(perm));
    ::foundation::core::View<std::uint64_t const> const all = ::fixy::mint_borrowed(region);

    auto parts = ::fixy::mint_split<8>(std::move(region));
    auto& [s0, s1, s2, s3, s4, s5, s6, s7] = parts.shards;

    // With fewer elements than shards, each of the first five shards takes
    // one element.  The last three are empty, and each starts at the end
    // of the region, not past it.
    CRUCIBLE_TEST_ASSERT(test, s0.size() == 1);
    CRUCIBLE_TEST_ASSERT(test, s4.size() == 1);
    CRUCIBLE_TEST_ASSERT(test, s5.size() == 0);
    CRUCIBLE_TEST_ASSERT(test, s6.size() == 0);
    CRUCIBLE_TEST_ASSERT(test, s7.size() == 0);
    CRUCIBLE_TEST_ASSERT(test, s5.empty());
    CRUCIBLE_TEST_ASSERT(test, starts_at(all, s0, 0));
    CRUCIBLE_TEST_ASSERT(test, starts_at(all, s5, 5));
    CRUCIBLE_TEST_ASSERT(test, starts_at(all, s7, 5));
}

// One shard of 800 elements in 8 shards: it holds the 100 positions of the
// range of its slice index.
template <typename Shard>
void check_slice_range(TestRunnerCtx const& test, Shard const& shard) {
    constexpr std::size_t shard_index = Shard::tag_type::index;
    CRUCIBLE_TEST_ASSERT(test, shard.size() == 100);
    CRUCIBLE_TEST_ASSERT(test, holds_positions_from(shard, shard_index * 100));
}

// The join, spelled out once: every shard holds the positions of its own
// range, and the shards are then surrendered to recombine, which folds their
// Slice permissions back into the parent's.  The scan of the shards shows
// both that no two shards hold one element and that each element is in a
// shard.
void test_split_then_rebuild_through_recombine(TestRunnerCtx const& test) {
    Arena arena;
    constexpr std::size_t N = 800;  // 8 × 100, exact division
    auto perm = mint_permission_root<DataA>();
    using Whole = OwnedRegion<std::uint64_t, DataA, decltype(perm)::brand_type>;
    auto region = Whole::adopt(test_alloc_token(), arena, N, std::move(perm));

    // Each element holds its own position, so the elements of a shard tell
    // the range that the split gave it.
    auto seeded = ::fixy::with_mut(std::move(region), [](auto run) noexcept {
        std::uint64_t position = 0;
        for (std::uint64_t& element : run)
            element = position++;
    });

    ::foundation::core::View<std::uint64_t const> const all = ::fixy::mint_borrowed(seeded);
    const std::size_t count = seeded.size();

    auto parts = ::fixy::mint_split<8>(std::move(seeded));
    std::apply([&test](auto const&... shard) { (check_slice_range(test, shard), ...); }, parts.shards);

    // The receipt the split wrote is surrendered beside the shards, and
    // it is what tells recombine that one split produced them.
    auto recombined = Whole::recombine(std::move(parts.witness), std::move(parts.shards));

    // recombine derives both from the shards rather than being told, so
    // check it recovered the extent the split started from.
    CRUCIBLE_TEST_ASSERT(test, starts_at(all, recombined, 0));
    CRUCIBLE_TEST_ASSERT(test, recombined.size() == count);
    CRUCIBLE_TEST_ASSERT(test, recombined.size() == N);
    CRUCIBLE_TEST_ASSERT(test, holds_positions_from(recombined, 0));
}

}  // namespace test_owned_region
