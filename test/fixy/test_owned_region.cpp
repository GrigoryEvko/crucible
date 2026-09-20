// The claim these tests exist to support: several threads may write to
// one buffer, with no atomics and no locks in the worker bodies, and the
// result is still well defined.
//
// That holds because a split hands each worker a sub-region over a
// distinct range of the same buffer, the permission tags prove those
// ranges are disjoint, and the only synchronization is the join at the
// end of the fork.  So the tests below are mostly about disjointness and
// about the chunk arithmetic that produces it: every element written
// exactly once, nothing skipped, nothing touched twice.
//
// Ported from test/test_owned_region.cpp without the parallel_for_views,
// parallel_reduce_views and parallel_apply_pair cells, whose helpers live
// in safety/Workload.h and are not ported; the join they performed is
// spelled out once below through the public door.  The detection cells
// of test/test_is_owned_region.cpp are folded in.  The crucible Arena
// is replaced by a bump allocator over a fixed block, which is all
// adopt reads of an arena.

#include <fixy/OwnedRegion.h>

#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>

using ::fixy::OwnedRegion;
using ::fixy::Slice;
using ::foundation::permissions::mint_permission_root;
using ::foundation::permissions::splits_into_pack_v;

struct TestFailure {};

#define CRUCIBLE_TEST_REQUIRE(...)                                                        \
    do {                                                                                  \
        if (!(__VA_ARGS__)) [[unlikely]] {                                                \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #__VA_ARGS__, __FILE__, __LINE__); \
            throw TestFailure{};                                                          \
        }                                                                                 \
    } while (0)

namespace {

int total_passed = 0;
int total_failed = 0;

template <typename F>
void run_test(const char* name, F&& body) {
    std::fprintf(stderr, "  %s: ", name);
    try {
        body();
        ++total_passed;
        std::fprintf(stderr, "PASSED\n");
    } catch (TestFailure&) {
        ++total_failed;
        std::fprintf(stderr, "FAILED\n");
    }
}

struct DataA {
    using permission_row = ::foundation::effects::Row<>;
};
struct DataB {
    using permission_row = ::foundation::effects::Row<>;
};

// One bump pointer over a fixed block: the smallest thing adopt asks
// of an arena.
class Arena {
    alignas(std::max_align_t) unsigned char block_[1 << 16]{};
    std::size_t used_ = 0;

public:
    template <typename T>
    [[nodiscard]] T* alloc_array(::foundation::effects::Alloc, std::size_t n) noexcept {
        if (n == 0) return nullptr;
        const std::size_t misalign = used_ % alignof(T);
        const std::size_t start = misalign == 0 ? used_ : used_ + (alignof(T) - misalign);
        const std::size_t nbytes = n * sizeof(T);
        if (start + nbytes > sizeof(block_)) std::abort();
        used_ = start + nbytes;
        return std::start_lifetime_as_array<T>(block_ + start, n);
    }
};
static_assert(::fixy::ArrayArena<Arena, float>);

inline ::foundation::effects::Alloc test_alloc_token() noexcept { return ::foundation::effects::Alloc{}; }

void test_compile_time_properties() {
    // A region is a pointer and a count.  The permission token costs
    // nothing because it has no state to store.
    static_assert(sizeof(OwnedRegion<float, DataA>) == sizeof(float*) + sizeof(std::size_t));
    static_assert(sizeof(OwnedRegion<std::uint64_t, DataA>) == sizeof(std::uint64_t*) + sizeof(std::size_t));

    static_assert(!std::is_copy_constructible_v<OwnedRegion<float, DataA>>);
    static_assert(std::is_move_constructible_v<OwnedRegion<float, DataA>>);
    static_assert(std::is_nothrow_move_constructible_v<OwnedRegion<float, DataA>>);

    // One door: the constructor is private, and both factories take
    // the permission by rvalue.
    static_assert(!std::is_constructible_v<OwnedRegion<float, DataA>, float*, std::size_t,
                                           ::foundation::permissions::Permission<DataA>&&>);

    // The split relation is generated rather than written out, so these
    // three widths stand in for any N.
    static_assert(splits_into_pack_v<DataA, Slice<DataA, 0>, Slice<DataA, 1>>);
    static_assert(splits_into_pack_v<DataA, Slice<DataA, 0>, Slice<DataA, 1>, Slice<DataA, 2>, Slice<DataA, 3>>);
    static_assert(splits_into_pack_v<DataA, Slice<DataA, 0>, Slice<DataA, 1>, Slice<DataA, 2>, Slice<DataA, 3>,
                                     Slice<DataA, 4>, Slice<DataA, 5>, Slice<DataA, 6>, Slice<DataA, 7>>);

    // The detection surface, with the cv-ref strip, and the two
    // extractors.
    using OR_int_x = OwnedRegion<int, DataA>;
    using OR_float_x = OwnedRegion<float, DataA>;
    using OR_int_y = OwnedRegion<int, DataB>;
    static_assert(::fixy::is_owned_region_v<OR_int_x>);
    static_assert(::fixy::is_owned_region_v<OR_float_x>);
    static_assert(::fixy::is_owned_region_v<OR_int_y>);
    static_assert(::fixy::is_owned_region_v<OR_int_x&>);
    static_assert(::fixy::is_owned_region_v<OR_int_x&&>);
    static_assert(::fixy::is_owned_region_v<OR_int_x const>);
    static_assert(::fixy::is_owned_region_v<OR_int_x const&>);
    static_assert(::fixy::is_owned_region_v<OR_int_x const&&>);
    static_assert(::fixy::is_owned_region_v<OR_int_x volatile>);
    static_assert(::fixy::is_owned_region_v<OR_int_x const volatile>);
    static_assert(!::fixy::is_owned_region_v<int>);
    static_assert(!::fixy::is_owned_region_v<int*>);
    static_assert(!::fixy::is_owned_region_v<int&>);
    static_assert(!::fixy::is_owned_region_v<int&&>);
    static_assert(!::fixy::is_owned_region_v<void>);
    static_assert(!::fixy::is_owned_region_v<DataA>);
    static_assert(::fixy::IsOwnedRegion<OR_int_y const&>);
    static_assert(std::is_same_v<::fixy::owned_region_value_t<OR_float_x&&>, float>);
    static_assert(std::is_same_v<::fixy::owned_region_tag_t<OR_int_y const&>, DataB>);
}

void test_adopt_and_view() {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    auto region = OwnedRegion<float, DataA>::adopt(test_alloc_token(), arena, 64, std::move(perm));

    CRUCIBLE_TEST_REQUIRE(region.size() == 64);
    CRUCIBLE_TEST_REQUIRE(!region.empty());
    CRUCIBLE_TEST_REQUIRE(region.data() != nullptr);
    CRUCIBLE_TEST_REQUIRE(region.span().size() == 64);

    for (std::size_t i = 0; i < 64; ++i)
        region.span()[i] = static_cast<float>(i);
    // Compared as bits rather than as floats, because -Werror=float-equal
    // forbids the direct comparison.  These small integers survive the
    // round trip through float exactly, so the bit compare is sound.
    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t got = std::bit_cast<std::uint32_t>(region.cspan()[i]);
        const std::uint32_t exp = std::bit_cast<std::uint32_t>(static_cast<float>(i));
        CRUCIBLE_TEST_REQUIRE(got == exp);
    }
}

void test_wrap_borrows_storage() {
    std::uint64_t storage[6] = {1, 2, 3, 4, 5, 6};
    auto region = OwnedRegion<std::uint64_t, DataB>::wrap(storage, 6, mint_permission_root<DataB>());

    // The region proves ownership of the bytes but does not own the
    // storage, so a write through it lands in the caller's array.
    CRUCIBLE_TEST_REQUIRE(region.data() == storage);
    region.span()[5] = 60;
    CRUCIBLE_TEST_REQUIRE(storage[5] == 60);

    std::uint64_t sum = 0;
    for (auto v : region)
        sum += v;
    CRUCIBLE_TEST_REQUIRE(sum == 1 + 2 + 3 + 4 + 5 + 60);
}

void test_split_into_chunk_math() {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(test_alloc_token(), arena, 1000, std::move(perm));

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
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(test_alloc_token(), arena, 1001, std::move(perm));

    auto parts = ::fixy::mint_split<8>(std::move(region));
    auto& [s0, s1, s2, s3, s4, s5, s6, s7] = parts.shards;

    // The chunk size rounds up, so the leading shards take 126 each and
    // the last one takes whatever is left, which is fewer.
    CRUCIBLE_TEST_REQUIRE(s0.size() == 126);
    CRUCIBLE_TEST_REQUIRE(s6.size() == 126);
    CRUCIBLE_TEST_REQUIRE(s7.size() == 119);
}

void test_split_smaller_than_n() {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(test_alloc_token(), arena, 5, std::move(perm));

    auto parts = ::fixy::mint_split<8>(std::move(region));
    auto& [s0, s1, s2, s3, s4, s5, s6, s7] = parts.shards;

    // With fewer elements than shards the rounded-up chunk is one, so
    // the shards past the end are empty rather than out of range.
    CRUCIBLE_TEST_REQUIRE(s0.size() == 1);
    CRUCIBLE_TEST_REQUIRE(s4.size() == 1);
    CRUCIBLE_TEST_REQUIRE(s5.size() == 0);
    CRUCIBLE_TEST_REQUIRE(s6.size() == 0);
    CRUCIBLE_TEST_REQUIRE(s7.size() == 0);
    CRUCIBLE_TEST_REQUIRE(s5.empty());
}

// The join the old parallel helpers performed, spelled out once: every
// shard writes its own slice index over its range, and the shards are
// then surrendered to recombine, which folds their Slice permissions
// back into the parent's.  The post-join scan shows both that no shard
// wrote outside its range and that no element went unwritten.
void test_split_then_rebuild_through_recombine() {
    Arena arena;
    constexpr std::size_t N = 800;  // 8 × 100, exact division
    auto region = OwnedRegion<std::uint64_t, DataA>::adopt(test_alloc_token(), arena, N, mint_permission_root<DataA>());

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
    auto recombined =
        OwnedRegion<std::uint64_t, DataA>::recombine(std::move(parts.witness), std::move(parts.shards));

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

// The brand travels with the region: a region minted from a branded
// permission carries that brand, so do the shards of its split, so does
// a borrow of it, and the recombined whole carries it back out.  The
// erased spelling is reachable from each, one way.
void test_brand_travels_through_split_and_recombine() {
    static std::uint64_t storage[8] = {};
    auto perm = mint_permission_root<DataA>();
    using Brand = ::foundation::brand::brand_of_t<decltype(perm)>;
    auto region = ::fixy::mint_owned_region(storage, std::size_t{8}, std::move(perm));
    static_assert(std::is_same_v<decltype(region), OwnedRegion<std::uint64_t, DataA, Brand>>);

    auto borrow = ::fixy::mint_borrowed(region);
    static_assert(::foundation::brand::SameBrand<decltype(borrow), decltype(region)>);
    CRUCIBLE_TEST_REQUIRE(borrow.size() == 8);

    auto parts = ::fixy::mint_split<2>(std::move(region));
    auto& [s0, s1] = parts.shards;
    static_assert(::foundation::brand::SameBrand<decltype(s0), decltype(borrow)>);
    static_assert(::foundation::brand::SameBrand<decltype(s1), decltype(borrow)>);
    CRUCIBLE_TEST_REQUIRE(s0.size() == 4 && s1.size() == 4);

    // The receipt names the region that was split, so its brand is the
    // region's and its shard count is the split's.
    static_assert(std::is_same_v<decltype(parts.witness)::brand_type, Brand>);
    static_assert(decltype(parts.witness)::shard_count == 2);

    auto whole =
        OwnedRegion<std::uint64_t, DataA, Brand>::recombine(std::move(parts.witness), std::move(parts.shards));
    static_assert(::foundation::brand::SameBrand<decltype(whole), decltype(borrow)>);
    CRUCIBLE_TEST_REQUIRE(whole.size() == 8);
    CRUCIBLE_TEST_REQUIRE(whole.data() == storage);

    OwnedRegion<std::uint64_t, DataA> erased = std::move(whole);
    CRUCIBLE_TEST_REQUIRE(erased.data() == storage);
}

// A zero-length request is the one arm of adopt that asks the arena for
// nothing.  The region it returns has to answer as empty on all three
// queries, because a null pointer with a non-zero count would read as a
// live region.
void test_adopt_zero_length() {
    Arena arena;
    auto region = OwnedRegion<int, DataA>::adopt(test_alloc_token(), arena, 0, mint_permission_root<DataA>());

    CRUCIBLE_TEST_REQUIRE(region.empty());
    CRUCIBLE_TEST_REQUIRE(region.size() == 0);
    CRUCIBLE_TEST_REQUIRE(region.data() == nullptr);
    CRUCIBLE_TEST_REQUIRE(region.span().empty());
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_owned_region:\n");

    test_compile_time_properties();  // pure compile-time

    run_test("test_adopt_zero_length", test_adopt_zero_length);
    run_test("test_adopt_and_view", test_adopt_and_view);
    run_test("test_wrap_borrows_storage", test_wrap_borrows_storage);
    run_test("test_split_into_chunk_math", test_split_into_chunk_math);
    run_test("test_split_uneven", test_split_uneven);
    run_test("test_split_smaller_than_n", test_split_smaller_than_n);
    run_test("test_split_then_rebuild_through_recombine", test_split_then_rebuild_through_recombine);
    run_test("test_brand_travels_through_split_and_recombine", test_brand_travels_through_split_and_recombine);

    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
