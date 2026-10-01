#pragma once

// The shared part of test_owned_region: the failure of a check, the two
// permission tags, the bump arena, the split of every total into one shard
// count, and the tests of each other source file.  test_owned_region.cpp
// lists the source files of the test.

#include <fixy/OwnedRegion.h>
#include <foundation/Lifetime.h>

#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <tuple>
#include <utility>

struct TestFailure {};

#define CRUCIBLE_TEST_REQUIRE(...)                                                        \
    do {                                                                                  \
        if (!(__VA_ARGS__)) [[unlikely]] {                                                \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #__VA_ARGS__, __FILE__, __LINE__); \
            throw TestFailure{};                                                          \
        }                                                                                 \
    } while (0)

namespace test_owned_region {

using ::fixy::OwnedRegion;
using ::fixy::Slice;
using ::foundation::permissions::can_split_into_pack_v;
using ::foundation::permissions::mint_permission_root;

struct DataA {
    using permission_row = ::foundation::effects::Row<>;
};
struct DataB {
    using permission_row = ::foundation::effects::Row<>;
};

// One bump pointer over a fixed block: the smallest thing adopt asks
// of an arena.  The lifetime start gives a live object only to a type
// whose every subobject is an implicit-lifetime type, so the constraint
// refuses every other type.
class Arena {
    alignas(std::max_align_t) unsigned char block_[1 << 16]{};
    std::size_t used_ = 0;

public:
    template <typename T>
        requires ::foundation::lifetime::ImplicitLifetimeThroughout<T>
    [[nodiscard]] T* alloc_array(::foundation::effects::Alloc, std::size_t n) noexcept {
        if (n == 0) return nullptr;
        const std::size_t misalign = used_ % alignof(T);
        const std::size_t start = misalign == 0 ? used_ : used_ + (alignof(T) - misalign);
        const std::size_t nbytes = n * sizeof(T);
        if (start + nbytes > sizeof(block_)) std::abort();
        used_ = start + nbytes;
        return ::foundation::lifetime::start_as_array<T>(block_ + start, n).data();
    }
};

inline ::foundation::effects::Alloc test_alloc_token() noexcept { return ::foundation::effects::Alloc{}; }

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
void split_and_recombine_every_total() {
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
                        CRUCIBLE_TEST_REQUIRE(byte_offset_of(shard, base) == next_start * sizeof(std::uint32_t));
                        CRUCIBLE_TEST_REQUIRE(shard.size() == total / N || shard.size() == total / N + 1);
                        for (auto& element : shard.span())
                            element = static_cast<std::uint32_t>(Is);
                        sizes[Is] = shard.size();
                        next_start += shard.size();
                    }(),
                    ...);
            }(std::make_index_sequence<N>{});
            CRUCIBLE_TEST_REQUIRE(next_start == total);

            // recombine accepts every split, and the whole it gives back
            // holds each shard's index exactly over that shard's range.
            auto whole = Region::recombine(std::move(parts.witness), std::move(parts.shards));
            CRUCIBLE_TEST_REQUIRE(whole.data() == base);
            CRUCIBLE_TEST_REQUIRE(whole.size() == total);
            std::size_t position = 0;
            for (std::size_t shard_index = 0; shard_index < N; ++shard_index) {
                for (std::size_t element = 0; element < sizes[shard_index]; ++element, ++position) {
                    CRUCIBLE_TEST_REQUIRE(whole.cspan()[position] == shard_index);
                }
            }
        }
    }
}

// Every total from 0 to 64 into the shard counts of one range
// (test_owned_region_every_total_<k>.cpp).
void split_and_recombine_every_total_of_counts_1_to_9();
void split_and_recombine_every_total_of_counts_10_to_13();
void split_and_recombine_every_total_of_counts_14_to_16();

// Splits into shards and the rebuild of the whole
// (test_owned_region_split.cpp).
void test_split_into_chunk_math();
void test_split_uneven();
void test_split_smaller_than_n();
void test_split_then_rebuild_through_recombine();

}  // namespace test_owned_region
