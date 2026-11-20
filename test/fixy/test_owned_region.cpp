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
// The join is spelled out once through the public door.  The arena is a
// bump allocator over a fixed block, which is all adopt reads of an arena.
//
// The test is several source files of one executable, so that no
// translation unit holds every test:
//
//   owned_region.h                the shared part
//   this file                     the compile-time properties, adopt,
//                                 wrap, the brand, the refusals of
//                                 recombine, the doors that empty a
//                                 region, the driver of every total and
//                                 count, and main
//   ..._split.cpp                 splits into shards and the rebuild of
//                                 the whole
//   ..._every_total_<k>.cpp       every total over the shard counts of
//                                 range k

#include "owned_region.h"

#include <fixy/Core.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <tuple>
#include <type_traits>
#include <utility>

namespace test_owned_region {

static_assert(::fixy::ArrayArena<Arena, float>);
static_assert(!::fixy::ArrayArena<Arena, ::foundation::permissions::Permission<DataA>>,
              "the arena must not start the lifetime of a proof type over its bytes");

// The door refuses a proof type, and an aggregate that holds one, also for an
// allocator that puts no constraint on its element.  The proof has a brand of
// its own, so the check names no erased identity.
struct ProofBrand final {};
using BrandedProof = ::foundation::permissions::Permission<DataA, ProofBrand>;
struct HoldsPermission {
    BrandedProof proof;
};
static_assert(::fixy::ArrayArena<UncheckedArena, float>);
static_assert(!::fixy::ArrayArena<UncheckedArena, BrandedProof>,
              "the door must refuse a proof type also for an allocator that puts no constraint on it");
static_assert(!::fixy::ArrayArena<UncheckedArena, HoldsPermission>,
              "the door must refuse an aggregate that holds a proof type also for an allocator that puts no "
              "constraint on it");

namespace {

using ::fixy::check::expect_dies;
using ::fixy::check::pass;

// The report of a recombine of shards that do not rebuild one region.
constexpr ::fixy::TextView kRefusedRecombine{"foundation: fatal invariant failed: contiguous"};

// The report of an adopt whose arena gives a run of a different count.
constexpr ::fixy::TextView kRefusedRun{"foundation: fatal invariant failed: run.size() == count"};

// A lookalike declares the two member types that the extractors read, so
// a refusal of it is the constraint's and not a missing member's.
struct RegionLookalike {
    using value_type = int;
    using tag_type = DataA;
};
template <typename T>
concept HasRegionValue = requires { typename ::fixy::owned_region_value_t<T>; };
template <typename T>
concept HasRegionTag = requires { typename ::fixy::owned_region_tag_t<T>; };

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
    static_assert(can_split_into_pack_v<DataA, Slice<DataA, 0>, Slice<DataA, 1>>);
    static_assert(can_split_into_pack_v<DataA, Slice<DataA, 0>, Slice<DataA, 1>, Slice<DataA, 2>, Slice<DataA, 3>>);
    static_assert(can_split_into_pack_v<DataA, Slice<DataA, 0>, Slice<DataA, 1>, Slice<DataA, 2>, Slice<DataA, 3>,
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
    static_assert(HasRegionValue<OR_int_x> && HasRegionTag<OR_int_x>);
    static_assert(!HasRegionValue<RegionLookalike> && !HasRegionTag<RegionLookalike>);
    static_assert(!HasRegionValue<int> && !HasRegionTag<void>);
}

void test_adopt_and_view(TestRunnerCtx const& test) {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    using Brand = decltype(perm)::brand_type;
    auto region = OwnedRegion<float, DataA, Brand>::adopt(test_alloc_token(), arena, 64, std::move(perm));

    CRUCIBLE_TEST_ASSERT(test, region.size() == 64);
    CRUCIBLE_TEST_ASSERT(test, !region.empty());
    CRUCIBLE_TEST_ASSERT(test, region.data() != nullptr);
    CRUCIBLE_TEST_ASSERT(test, region.span().size() == 64);

    for (std::size_t i = 0; i < 64; ++i)
        region.span()[i] = static_cast<float>(i);
    // Compared as bits rather than as floats, because -Werror=float-equal
    // forbids the direct comparison.  These small integers survive the
    // round trip through float exactly, so the bit compare is sound.
    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t got = std::bit_cast<std::uint32_t>(region.cspan()[i]);
        const std::uint32_t exp = std::bit_cast<std::uint32_t>(static_cast<float>(i));
        CRUCIBLE_TEST_ASSERT(test, got == exp);
    }
}

// The arena door ends the process when the arena gives a run of a count
// other than the count of the request, because the region would then cover
// elements that the arena did not give.  The unchecked arena gives the empty
// View for each request.  The adopt runs in a child through expect_dies of
// the Check family.
void test_adopt_refuses_a_run_of_another_count(TestRunnerCtx const& test) {
    expect_dies(
        test,
        [] noexcept {
            UncheckedArena arena{};
            auto perm = mint_permission_root<DataB>();
            using Brand = decltype(perm)::brand_type;
            auto region = OwnedRegion<float, DataB, Brand>::adopt(test_alloc_token(), arena, 4, std::move(perm));
            ::foundation::linear::drop(std::move(region).release());
        },
        kRefusedRun);
}

void test_wrap_borrows_storage(TestRunnerCtx const& test) {
    std::uint64_t storage[6] = {1, 2, 3, 4, 5, 6};
    auto perm = mint_permission_root<DataB>();
    using Brand = decltype(perm)::brand_type;
    auto region = OwnedRegion<std::uint64_t, DataB, Brand>::wrap(storage, 6, std::move(perm));

    // The region proves ownership of the bytes but does not own the
    // storage, so a write through it lands in the caller's array.
    CRUCIBLE_TEST_ASSERT(test, region.data() == storage);
    region.span()[5] = 60;
    CRUCIBLE_TEST_ASSERT(test, storage[5] == 60);

    std::uint64_t sum = 0;
    for (auto v : region)
        sum += v;
    CRUCIBLE_TEST_ASSERT(test, sum == 1 + 2 + 3 + 4 + 5 + 60);
}

// Every total from 0 to 64 into every shard count from 1 to 16.  A split
// that puts a shard past the end of its region, or leaves a gap between
// two shards, fails the offset check, and recombine aborts on it.  Each
// range of shard counts is in a source file of its own.
void test_split_and_recombine_every_total_and_count(TestRunnerCtx const& test) {
    split_and_recombine_every_total_of_counts_1_to_9(test);
    split_and_recombine_every_total_of_counts_10_to_13(test);
    split_and_recombine_every_total_of_counts_14_to_16(test);
}

// The brand travels with the region: a region minted from a branded
// permission carries that brand, so do the shards of its split, and the
// recombined whole carries it back out.  No conversion reaches the erased
// spelling from any of them.  A borrow of the region is a View, which
// names no brand.
void test_brand_travels_through_split_and_recombine(TestRunnerCtx const& test) {
    static std::uint64_t storage[8] = {};
    auto perm = mint_permission_root<DataA>();
    using Brand = ::foundation::brand::brand_of_t<decltype(perm)>;
    auto region = ::fixy::mint_owned_region(storage, std::size_t{8}, std::move(perm));
    static_assert(std::is_same_v<decltype(region), OwnedRegion<std::uint64_t, DataA, Brand>>);
    using Region = decltype(region);

    auto borrow = ::fixy::mint_borrowed(region);
    static_assert(std::is_same_v<decltype(borrow), ::fixy::View<std::uint64_t>>);
    CRUCIBLE_TEST_ASSERT(test, borrow.size() == 8);

    auto parts = ::fixy::mint_split<2>(std::move(region));
    auto& [s0, s1] = parts.shards;
    static_assert(::foundation::brand::SameBrand<decltype(s0), Region>);
    static_assert(::foundation::brand::SameBrand<decltype(s1), Region>);
    CRUCIBLE_TEST_ASSERT(test, s0.size() == 4 && s1.size() == 4);

    // The receipt names the region that was split, so its brand is the
    // region's and its shard count is the split's.
    static_assert(std::is_same_v<decltype(parts.witness)::brand_type, Brand>);
    static_assert(decltype(parts.witness)::shard_count == 2);

    auto whole = OwnedRegion<std::uint64_t, DataA, Brand>::recombine(std::move(parts.witness), std::move(parts.shards));
    static_assert(::foundation::brand::SameBrand<decltype(whole), Region>);
    CRUCIBLE_TEST_ASSERT(test, whole.size() == 8);
    CRUCIBLE_TEST_ASSERT(test, whole.data() == storage);
    static_assert(!std::is_constructible_v<OwnedRegion<std::uint64_t, DataA>, decltype(whole)&&>,
                  "a branded region does not erase");
}

// A split inside one function is one split site, so every call gives
// shards and a receipt of one type.  A rebuild from shard 0 of one region
// and shard 1 of another is well typed, and recombine aborts on it, because
// shard 1 does not start where shard 0 ends.  A region on the erased brand
// does not split at all: the split of its token refuses the erased identity
// (neg_permission_split_of_a_singleton_root).
auto split_branded_in_two(std::uint64_t* storage) {
    return ::fixy::mint_split<2>(::fixy::mint_owned_region(storage, std::size_t{4}, mint_permission_root<DataA>()));
}

// The mixed tuple takes shard 1 of the second region, so the second
// region cannot recombine after it.  A third split at the same site shows
// that its own shards still recombine.
// The recombine of the mixed tuple runs in a child through expect_dies of the
// Check family.
template <typename Split>
void require_mixed_rebuild_aborts(TestRunnerCtx const& test, Split split) {
    static std::uint64_t first[4] = {};
    static std::uint64_t second[4] = {};
    static std::uint64_t third[4] = {};
    auto parts_a = split(first);
    auto parts_b = split(second);
    static_assert(std::is_same_v<decltype(parts_a), decltype(parts_b)>, "one split site is one type");
    using Whole = std::remove_cvref_t<decltype(std::get<0>(parts_a.shards))>::brand_type;
    using Region = OwnedRegion<std::uint64_t, DataA, Whole>;
    expect_dies(
        test,
        [&] noexcept {
            auto mixed = std::tuple{std::move(std::get<0>(parts_a.shards)), std::move(std::get<1>(parts_b.shards))};
            (void)Region::recombine(std::move(parts_a.witness), std::move(mixed));
        },
        kRefusedRecombine);
    auto parts_c = split(third);
    auto whole = Region::recombine(std::move(parts_c.witness), std::move(parts_c.shards));
    CRUCIBLE_TEST_ASSERT(test, whole.data() == third && whole.size() == 4);
}

// A shard moved out of the tuple before the recombine is empty at a null
// base, so it breaks the chain of shards and recombine aborts.  A shard
// that kept its base and its count would write the bytes of the rebuilt
// whole.  The use-after-move guard does not see this use, because it
// moves a tuple element and then the tuple.
template <std::size_t KeptIndex>
void require_recombine_aborts_without_shard(TestRunnerCtx const& test) {
    static std::uint64_t storage[4] = {};
    auto parts = split_branded_in_two(storage);
    using Region = OwnedRegion<std::uint64_t, DataA,
                               typename std::remove_cvref_t<decltype(std::get<0>(parts.shards))>::brand_type>;
    expect_dies(
        test,
        [&] noexcept {
            auto kept = std::move(std::get<KeptIndex>(parts.shards));
            (void)Region::recombine(std::move(parts.witness), std::move(parts.shards));
            kept.span()[0] = 7;
        },
        kRefusedRecombine);
}

void test_recombine_refuses_a_shard_moved_out_of_the_tuple(TestRunnerCtx const& test) {
    require_recombine_aborts_without_shard<0>(test);
    require_recombine_aborts_without_shard<1>(test);
}

// One mint site is one brand, so two regions from this helper are one
// type and one can be assigned to the other.
auto branded_at_one_site(std::uint64_t* storage, std::size_t count) {
    return ::fixy::mint_owned_region(storage, count, mint_permission_root<DataA>());
}

// Each door that consumes a region leaves it empty: the move, the split,
// the recombine and the release.  No consumed region keeps the bytes that
// its successor owns.  A region has no move assignment, because the
// assignment would drop the token of the target.
void test_every_consuming_door_empties_the_region(TestRunnerCtx const& test) {
    static std::uint64_t storage[4] = {1, 2, 3, 4};
    auto source = branded_at_one_site(storage, 4);
    using Branded = decltype(source);

    Branded moved_into = std::move(source);
    CRUCIBLE_TEST_ASSERT(test, source.empty() && source.data() == nullptr);
    CRUCIBLE_TEST_ASSERT(test, moved_into.data() == storage && moved_into.size() == 4);

    auto parts = ::fixy::mint_split<2>(std::move(moved_into));
    CRUCIBLE_TEST_ASSERT(test, moved_into.empty() && moved_into.data() == nullptr);

    auto whole = Branded::recombine(std::move(parts.witness), std::move(parts.shards));
    CRUCIBLE_TEST_ASSERT(test, std::get<0>(parts.shards).empty() && std::get<0>(parts.shards).data() == nullptr);
    CRUCIBLE_TEST_ASSERT(test, std::get<1>(parts.shards).empty() && std::get<1>(parts.shards).data() == nullptr);
    CRUCIBLE_TEST_ASSERT(test, whole.data() == storage && whole.size() == 4);

    // The release door gives the token back, and the token wraps the same
    // storage again.
    auto token = std::move(whole).release();
    CRUCIBLE_FATAL_INVARIANT(whole.empty() && whole.data() == nullptr);
    auto rewrapped = Branded::wrap(storage, 4, std::move(token));
    CRUCIBLE_FATAL_INVARIANT(rewrapped.data() == storage && rewrapped.size() == 4);
}

void test_recombine_refuses_shards_of_two_regions_of_one_site(TestRunnerCtx const& test) {
    require_mixed_rebuild_aborts(test, split_branded_in_two);
}

// A zero-length request is the one arm of adopt that asks the arena for
// nothing.  The region it returns has to answer as empty on all three
// queries, because a null pointer with a non-zero count would read as a
// live region.
void test_adopt_zero_length(TestRunnerCtx const& test) {
    Arena arena;
    auto perm = mint_permission_root<DataA>();
    using Brand = decltype(perm)::brand_type;
    auto region = OwnedRegion<int, DataA, Brand>::adopt(test_alloc_token(), arena, 0, std::move(perm));

    CRUCIBLE_TEST_ASSERT(test, region.empty());
    CRUCIBLE_TEST_ASSERT(test, region.size() == 0);
    CRUCIBLE_TEST_ASSERT(test, region.data() == nullptr);
    CRUCIBLE_TEST_ASSERT(test, region.span().empty());
}

}  // namespace

}  // namespace test_owned_region

using namespace test_owned_region;

int main() {
    TestRunnerCtx const test{::foundation::effects::testing::test()};
    pass(test, "test_owned_region:\n");

    test_compile_time_properties();  // pure compile-time

    test_adopt_zero_length(test);
    pass(test, "  test_adopt_zero_length: PASSED\n");
    test_adopt_and_view(test);
    pass(test, "  test_adopt_and_view: PASSED\n");
    test_adopt_refuses_a_run_of_another_count(test);
    pass(test, "  test_adopt_refuses_a_run_of_another_count: PASSED\n");
    test_wrap_borrows_storage(test);
    pass(test, "  test_wrap_borrows_storage: PASSED\n");
    test_split_into_chunk_math(test);
    pass(test, "  test_split_into_chunk_math: PASSED\n");
    test_split_uneven(test);
    pass(test, "  test_split_uneven: PASSED\n");
    test_split_smaller_than_n(test);
    pass(test, "  test_split_smaller_than_n: PASSED\n");
    test_split_and_recombine_every_total_and_count(test);
    pass(test, "  test_split_and_recombine_every_total_and_count: PASSED\n");
    test_split_then_rebuild_through_recombine(test);
    pass(test, "  test_split_then_rebuild_through_recombine: PASSED\n");
    test_brand_travels_through_split_and_recombine(test);
    pass(test, "  test_brand_travels_through_split_and_recombine: PASSED\n");
    test_recombine_refuses_shards_of_two_regions_of_one_site(test);
    pass(test, "  test_recombine_refuses_shards_of_two_regions_of_one_site: PASSED\n");
    test_recombine_refuses_a_shard_moved_out_of_the_tuple(test);
    pass(test, "  test_recombine_refuses_a_shard_moved_out_of_the_tuple: PASSED\n");
    test_every_consuming_door_empties_the_region(test);
    pass(test, "  test_every_consuming_door_empties_the_region: PASSED\n");
    pass(test, "test_owned_region: ALL PASSED\n");
    return 0;
}
