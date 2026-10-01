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

void test_split_into_chunk_math() {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    using Brand = decltype(perm)::brand_type;
    auto region = OwnedRegion<std::uint64_t, DataA, Brand>::adopt(test_alloc_token(), arena, 1000, std::move(perm));

    // Each element holds its own index, so a shard's contents identify
    // the offset it was cut from.
    for (std::size_t i = 0; i < 1000; ++i)
        region.span()[i] = i;

    auto parts = ::fixy::mint_split<8>(std::move(region));
    auto& [s0, s1, s2, s3, s4, s5, s6, s7] = parts.shards;

    // 1000 over 8 divides exactly, so every shard is the same size.
    CRUCIBLE_TEST_REQUIRE(s0.size() == 125);
    CRUCIBLE_TEST_REQUIRE(s1.size() == 125);
    CRUCIBLE_TEST_REQUIRE(s7.size() == 125);

    // The shards are views into one buffer, not copies, which is what
    // reading the seeded indices back at the right offsets shows.
    CRUCIBLE_TEST_REQUIRE(s0.cspan()[0] == 0);
    CRUCIBLE_TEST_REQUIRE(s0.cspan()[124] == 124);
    CRUCIBLE_TEST_REQUIRE(s1.cspan()[0] == 125);
    CRUCIBLE_TEST_REQUIRE(s7.cspan()[0] == 875);
    CRUCIBLE_TEST_REQUIRE(s7.cspan()[124] == 999);

    // Each shard carries its own slice index in its tag.
    static_assert(std::remove_cvref_t<decltype(s3)>::tag_type::index == 3);
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(s3)>::tag_type::parent_type, DataA>);
}

void test_split_uneven() {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    using Brand = decltype(perm)::brand_type;
    auto region = OwnedRegion<std::uint64_t, DataA, Brand>::adopt(test_alloc_token(), arena, 1001, std::move(perm));

    auto parts = ::fixy::mint_split<8>(std::move(region));
    auto& [s0, s1, s2, s3, s4, s5, s6, s7] = parts.shards;

    // 1001 over 8 leaves a remainder of one, so the first shard takes one
    // element more than the 125 that each other shard takes.
    CRUCIBLE_TEST_REQUIRE(s0.size() == 126);
    CRUCIBLE_TEST_REQUIRE(s1.size() == 125);
    CRUCIBLE_TEST_REQUIRE(s6.size() == 125);
    CRUCIBLE_TEST_REQUIRE(s7.size() == 125);
    CRUCIBLE_TEST_REQUIRE(s1.data() == s0.data() + 126);
    CRUCIBLE_TEST_REQUIRE(s7.data() + 125 == s0.data() + 1001);
}

void test_split_smaller_than_n() {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    using Brand = decltype(perm)::brand_type;
    auto region = OwnedRegion<std::uint64_t, DataA, Brand>::adopt(test_alloc_token(), arena, 5, std::move(perm));

    auto parts = ::fixy::mint_split<8>(std::move(region));
    auto& [s0, s1, s2, s3, s4, s5, s6, s7] = parts.shards;

    // With fewer elements than shards, each of the first five shards takes
    // one element.  The last three are empty, and each starts at the end
    // of the region, not past it.
    CRUCIBLE_TEST_REQUIRE(s0.size() == 1);
    CRUCIBLE_TEST_REQUIRE(s4.size() == 1);
    CRUCIBLE_TEST_REQUIRE(s5.size() == 0);
    CRUCIBLE_TEST_REQUIRE(s6.size() == 0);
    CRUCIBLE_TEST_REQUIRE(s7.size() == 0);
    CRUCIBLE_TEST_REQUIRE(s5.empty());
    CRUCIBLE_TEST_REQUIRE(s5.data() == s0.data() + 5);
    CRUCIBLE_TEST_REQUIRE(s7.data() == s0.data() + 5);
}

// The join, spelled out once: every shard writes its own slice index over its range, and the shards are
// then surrendered to recombine, which folds their Slice permissions
// back into the parent's.  The post-join scan shows both that no shard
// wrote outside its range and that no element went unwritten.
void test_split_then_rebuild_through_recombine() {
    Arena arena;
    constexpr std::size_t N = 800;  // 8 × 100, exact division
    auto perm = mint_permission_root<DataA>();
    using Whole = OwnedRegion<std::uint64_t, DataA, decltype(perm)::brand_type>;
    auto region = Whole::adopt(test_alloc_token(), arena, N, std::move(perm));

    // A value no shard index can produce, so an untouched element is
    // distinguishable from a written one.
    for (std::size_t i = 0; i < N; ++i)
        region.span()[i] = 0xDEAD;

    std::uint64_t* base = region.data();
    const std::size_t count = region.size();

    auto parts = ::fixy::mint_split<8>(std::move(region));
    std::apply(
        [](auto&... sub) {
            (
                [](auto& one) {
                    using SubT = std::remove_cvref_t<decltype(one)>;
                    constexpr std::size_t shard_idx = SubT::tag_type::index;
                    for (auto& x : one.span())
                        x = shard_idx;
                }(sub),
                ...);
        },
        parts.shards);

    // The receipt the split wrote is surrendered beside the shards, and
    // it is what tells recombine that one split produced them.
    auto recombined = Whole::recombine(std::move(parts.witness), std::move(parts.shards));

    // recombine derives both from the shards rather than being told, so
    // check it recovered the extent the split started from.
    CRUCIBLE_TEST_REQUIRE(recombined.data() == base);
    CRUCIBLE_TEST_REQUIRE(recombined.size() == count);
    CRUCIBLE_TEST_REQUIRE(recombined.size() == N);
    for (std::size_t shard = 0; shard < 8; ++shard) {
        for (std::size_t i = 0; i < 100; ++i) {
            CRUCIBLE_TEST_REQUIRE(recombined.cspan()[shard * 100 + i] == shard);
        }
    }
}

}  // namespace test_owned_region
