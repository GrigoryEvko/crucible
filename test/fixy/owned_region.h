#pragma once

// The shared part of test_owned_region: the two permission tags, the bump
// arena, the split of every total into one shard count, and the tests of
// each other source file.  owned_region_unchecked_arena.h gives the
// allocator with no constraint of its own.  test_owned_region.cpp lists the source files of
// the test.  Each test takes the test context, and each check is
// CRUCIBLE_TEST_ASSERT of the Check family.

#include <fixy/Ctx.h>
#include <fixy/OwnedRegion.h>
#include <fixy/check/Check.h>
#include <foundation/Lifetime.h>
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
#include <memory>
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

// The smallest thing adopt asks of an arena: a bump arena of the base, which
// gives each request a run that no other request holds.  The constraint
// admits only a type whose every subobject is an implicit-lifetime type, so
// it refuses each other type, as an arena of live objects must.  Each element
// of a run holds the value-initialized T.
class Arena {
    ::foundation::core::BumpArena bump_{std::size_t{1} << 16};

public:
    template <typename T>
        requires ::foundation::lifetime::ImplicitLifetimeThroughout<T>
    [[nodiscard]] ::foundation::core::View<T> alloc_array(::foundation::effects::Alloc alloc, std::size_t n) noexcept {
        return bump_.array(alloc, n, T{});
    }
};

// The allocation capability of the test context.
inline ::foundation::effects::Alloc test_alloc_token() noexcept { return ::foundation::effects::testing::test().alloc; }

// The byte offset of a shard from the start of its region.  It is read
// as an integer, so a shard that starts past the region gives a number
// that the caller can compare, not a pointer that is undefined to form.
template <typename Shard, typename T>
std::uintptr_t byte_offset_of(Shard const& shard, T const* base) noexcept {
    return std::bit_cast<std::uintptr_t>(shard.data()) - std::bit_cast<std::uintptr_t>(base);
}

// One shard count over every total from 0 to 64.  The storage is a heap
// block of exactly `total` elements, so AddressSanitizer reports a write
// past its end.  At a total of zero the block has one element, because the
// optimizer refuses an allocation of zero (-Werror=alloc-zero) and the empty
// region writes nothing.  A total of zero also runs over a null base, which
// is the region that an arena gives for a request of zero elements.
template <std::size_t N>
void split_and_recombine_every_total(TestRunnerCtx const& test) {
    for (std::size_t total = 0; total <= 64; ++total) {
        const bool also_null_base = total == 0;
        for (int pass = 0; pass < (also_null_base ? 2 : 1); ++pass) {
            auto storage = std::make_unique<std::uint32_t[]>(total == 0 ? 1 : total);
            std::uint32_t* const base = pass == 0 ? storage.get() : nullptr;
            auto region = ::fixy::mint_owned_region(base, total, mint_permission_root<DataA>());
            using Region = decltype(region);
            auto parts = ::fixy::mint_split<N>(std::move(region));

            // Each shard starts where the one before it ends, holds the
            // count or one element more than the count of every other
            // shard, and writes its own index over its range.
            std::array<std::size_t, N> sizes{};
            std::size_t next_start = 0;
            [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                (
                    [&] {
                        auto& shard = std::get<Is>(parts.shards);
                        CRUCIBLE_TEST_ASSERT(test, byte_offset_of(shard, base) == next_start * sizeof(std::uint32_t));
                        CRUCIBLE_TEST_ASSERT(test, shard.size() == total / N || shard.size() == total / N + 1);
                        for (auto& element : shard.span())
                            element = static_cast<std::uint32_t>(Is);
                        sizes[Is] = shard.size();
                        next_start += shard.size();
                    }(),
                    ...);
            }(std::make_index_sequence<N>{});
            CRUCIBLE_TEST_ASSERT(test, next_start == total);

            // recombine accepts every split, and the whole it gives back
            // holds each shard's index exactly over that shard's range.
            auto whole = Region::recombine(std::move(parts.witness), std::move(parts.shards));
            CRUCIBLE_TEST_ASSERT(test, whole.data() == base);
            CRUCIBLE_TEST_ASSERT(test, whole.size() == total);
            std::size_t position = 0;
            for (std::size_t shard_index = 0; shard_index < N; ++shard_index) {
                for (std::size_t element = 0; element < sizes[shard_index]; ++element, ++position) {
                    CRUCIBLE_TEST_ASSERT(test, whole.cspan()[position] == shard_index);
                }
            }
        }
    }
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
