#pragma once

// The shared part of test_owned_region: the permission tags, the bump
// arena, the split of every total into one shard count, and the tests of
// each other source file.  owned_region_unchecked_arena.h gives the
// allocator with no constraint of its own.  test_owned_region.cpp lists the source files of
// the test.  Each test takes the test context, and each check is
// CRUCIBLE_TEST_ASSERT of the Check family.

#include <fixy/Ctx.h>
#include <fixy/OwnedRegion.h>
#include <fixy/check/Check.h>
#include <foundation/Lifetime.h>
#include <foundation/core/Array.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Region.h>

#include "owned_region_unchecked_arena.h"

#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <tuple>
#include <utility>

namespace test_owned_region {

using ::fixy::OwnedRegion;
using ::fixy::Slice;
using ::fixy::TestRunnerCtx;
using ::foundation::permissions::can_split_into_pack_v;
using ::foundation::permissions::mint_permission_root;

struct DataA {
    using permission_row = ::foundation::effects::Row<>;
};
struct DataB {
    using permission_row = ::foundation::effects::Row<>;
};

// The tag of the regions of the region scope door.  A region scope door mints
// its tokens.
struct DataScope {
    using permission_row = ::foundation::effects::Row<>;
    using scope_door = ::fixy::RegionScopeDoor;
};

// The smallest thing adopt asks of an arena: a bump arena of the base, which
// gives each request a run that no other request holds.  The constraint
// admits only a type whose every subobject is an implicit-lifetime type, so
// it refuses each other type, as an arena of live objects must.  Each element
// of a run holds the value-initialized T.
// The bump arena is the one member of the class, so it takes a member tag
// that the class nests.
class Arena {
    struct[[= ::foundation::brand::arena_member_brand{}]] Bump final {};

    ::foundation::core::BumpArena<Bump> bump_ = ::foundation::core::mint_bump_arena<Bump>(
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{std::size_t{1} << 16});

public:
    template <typename T>
        requires ::foundation::lifetime::ImplicitLifetimeThroughout<T>
    [[nodiscard]] ::foundation::core::View<T> alloc_array(::foundation::effects::Alloc alloc, std::size_t n) noexcept {
        auto const run = bump_.array(alloc, n, T{});
        // A plain View of an arena run: the View carries no brand of its arena.
        return run.view();
    }
};

// The allocation capability of the test context.
inline ::foundation::effects::Alloc test_alloc_token() noexcept { return ::foundation::effects::testing::test().alloc; }

// True when the elements of a region lie inside the run of the whole and
// start at position.  offset_of of foundation/core/Region.h compares the
// two address ranges as integers, so a region that starts past the whole
// gives false, not a pointer that is undefined to form.
template <typename T, typename Region>
[[nodiscard]] bool starts_at(::foundation::core::View<T const> whole, Region const& region,
                             std::size_t position) noexcept {
    return ::foundation::core::offset_of(whole, ::fixy::mint_borrowed(region))
        == ::foundation::core::Option<std::size_t>::some(position);
}

// True when a region names no element of the run of the whole and no
// position in it: the region of a consuming door, which is empty at a null
// base.
template <typename T, typename Region>
[[nodiscard]] bool lies_outside(::foundation::core::View<T const> whole, Region const& region) noexcept {
    return region.empty() && ::foundation::core::offset_of(whole, ::fixy::mint_borrowed(region)).is_none();
}

// True when the elements of a region hold the positions from first on, one
// for each element: the elements of a shard of a run whose elements hold
// their own positions.  Complexity: O(region.size()).
template <typename Region>
[[nodiscard]] bool holds_positions_from(Region const& region, std::size_t first) noexcept {
    std::size_t position = first;
    for (auto const element : ::fixy::mint_borrowed(region)) {
        if (element != position) return false;
        ++position;
    }
    return true;
}

// The split of one region of total elements into N shards, and its rebuild.
// The function writes each element, checks the shards and the whole that
// recombine gives, and gives back that whole.  Complexity: O(total).
template <std::size_t N, typename Region>
[[nodiscard]] Region split_and_recombine_one_region(TestRunnerCtx const& test, std::size_t total,
                                                    Region region) noexcept {
    // Each element holds its own position, so the elements of a shard tell
    // the range that the split gave it.
    auto seeded = ::fixy::with_mut(std::move(region), [](auto run) noexcept {
        std::uint32_t position = 0;
        for (std::uint32_t& element : run)
            element = position++;
    });
    ::foundation::core::View<std::uint32_t const> const all = ::fixy::mint_borrowed(seeded);
    auto parts = ::fixy::mint_split<N>(std::move(seeded));

    // Each shard starts where the one before it ends, holds the count or one
    // element more than the count of every other shard, and holds the
    // positions of its own range.  So no two shards hold one element, and
    // each element is in a shard.
    std::size_t next_start = 0;
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        (
            [&] {
                auto& shard = std::get<Is>(parts.shards);
                CRUCIBLE_TEST_ASSERT(test, starts_at(all, shard, next_start));
                CRUCIBLE_TEST_ASSERT(test, shard.size() == total / N || shard.size() == total / N + 1);
                CRUCIBLE_TEST_ASSERT(test, holds_positions_from(shard, next_start));
                next_start += shard.size();
            }(),
            ...);
    }(std::make_index_sequence<N>{});
    CRUCIBLE_TEST_ASSERT(test, next_start == total);

    // recombine accepts every split, and the whole it gives back is the run
    // that the split cut.
    auto whole = Region::recombine(std::move(parts.witness), std::move(parts.shards));
    CRUCIBLE_TEST_ASSERT(test, starts_at(all, whole, 0));
    CRUCIBLE_TEST_ASSERT(test, whole.size() == total);
    CRUCIBLE_TEST_ASSERT(test, holds_positions_from(whole, 0));
    return whole;
}

// One shard count over every total from 0 to 64.  A region scope door covers
// a HeapArray of exactly `total` elements, so AddressSanitizer reports a
// write past its end.  At a total of zero the HeapArray holds no storage, so
// the region is at a null base, as the region that an arena gives for a
// request of zero elements.  A last run of zero elements starts at a base
// that is not null: the empty part of a split of a block of one element.
template <std::size_t N>
void split_and_recombine_every_total(TestRunnerCtx const& test) {
    for (std::size_t total = 0; total <= 64; ++total) {
        ::foundation::core::HeapArray<std::uint32_t> const storage = ::fixy::with_region<DataScope>(
            ::foundation::core::mint_heap_array(test_alloc_token(), total, std::uint32_t{0}),
            [&test, total](auto region) noexcept {
                return split_and_recombine_one_region<N>(test, total, std::move(region));
            });
        CRUCIBLE_TEST_ASSERT(test, storage.size() == total);
    }
    ::foundation::core::FixedArray<std::uint32_t, 1> block{};
    block.with_mut([&test](auto run) noexcept {
        auto [empty, rest] = ::foundation::core::split_at(std::move(run), 0).expect("each run holds the offset zero");
        static_cast<void>(rest);
        static_cast<void>(::fixy::with_region<DataScope>(std::move(empty), [&test](auto region) noexcept {
            return split_and_recombine_one_region<N>(test, 0, std::move(region));
        }));
    });
}

// Every total from 0 to 64 into the shard counts of one range
// (test_owned_region_every_total_<k>.cpp).
void split_and_recombine_every_total_of_counts_1_to_9(TestRunnerCtx const& test);
void split_and_recombine_every_total_of_counts_10_to_13(TestRunnerCtx const& test);
void split_and_recombine_every_total_of_counts_14_to_16(TestRunnerCtx const& test);

// Splits into shards and the rebuild of the whole
// (test_owned_region_split.cpp).
void test_split_into_chunk_math(TestRunnerCtx const& test);
void test_split_uneven(TestRunnerCtx const& test);
void test_split_smaller_than_n(TestRunnerCtx const& test);
void test_split_then_rebuild_through_recombine(TestRunnerCtx const& test);

}  // namespace test_owned_region
