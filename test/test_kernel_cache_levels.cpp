// The three levels of the kernel cache: L1 holds the vendor-neutral form,
// L2 the form for one vendor family, L3 the compiled bytes for one chip.
// Each level tags what it returns with the level as its provenance, so a
// value from one level is a different type from a value from another.

#include "test_kernels.h"

#include <crucible/KernelCache.h>
#include <fixy/Tagged.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <cstdio>
#include <expected>
#include <type_traits>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using crucible::ContentHash;
using crucible::KernelCache;
using crucible::RowHash;
using crucible::test::is_found;
using crucible::test::Kernels;
using crucible::test::make_kernel;
using crucible::test::make_kernel_arena;

using L1 = KernelCache::VendorNeutralLevel;
using L2 = KernelCache::VendorFamilyLevel;
using L3 = KernelCache::ChipLevel;
using PublishResult = KernelCache::InsertResult;
using Found = KernelCache::FoundKernel;

// The allocation capability of each cache of the test.
constexpr ::foundation::effects::Alloc kAlloc = ::foundation::effects::testing::test().alloc;

// The tag of Nondet of each lookup and each publish of the test.
constexpr ::foundation::effects::cap::Nondet kNondet = ::foundation::effects::testing::test().nondet;

// True when the publish was refused because its level has no store.
[[nodiscard]] static bool is_not_yet_implemented(PublishResult result) noexcept {
    return std::move(result).err().match(
        [](KernelCache::InsertError error) noexcept { return error == KernelCache::InsertError::NotYetImplemented; },
        []() noexcept { return false; });
}

static void test_lookup_l1_round_trip() {
    Kernels kernels = make_kernel_arena();
    KernelCache cache{kAlloc};
    auto const kernel = make_kernel(kernels);
    auto pub = cache.publish_l1(kNondet, ContentHash{0xAAAA}, RowHash{0}, kernel);
    CRUCIBLE_TEST_ASSERT(kTest, std::move(pub).into().is_ok());

    const Found raw = cache.lookup(kNondet, ContentHash{0xAAAA}, RowHash{0});
    auto tagged = cache.lookup_l1(kNondet, ContentHash{0xAAAA}, RowHash{0});
    const Found via_level = std::move(tagged).into();
    CRUCIBLE_TEST_ASSERT(kTest, is_found(raw, kernel));
    CRUCIBLE_TEST_ASSERT(kTest, is_found(via_level, kernel));
}

static void test_each_level_names_its_level() {
    Kernels kernels = make_kernel_arena();
    KernelCache cache{kAlloc};
    auto const kernel = make_kernel(kernels);
    static_assert(
        std::is_same_v<decltype(cache.lookup_l1(kNondet, ContentHash{1}, RowHash{0})), ::fixy::Tagged<Found, L1>>);
    static_assert(
        std::is_same_v<decltype(cache.lookup_l2(kNondet, ContentHash{1}, RowHash{0})), ::fixy::Tagged<Found, L2>>);
    static_assert(
        std::is_same_v<decltype(cache.lookup_l3(kNondet, ContentHash{1}, RowHash{0})), ::fixy::Tagged<Found, L3>>);
    static_assert(std::is_same_v<decltype(cache.publish_l1(kNondet, ContentHash{1}, RowHash{0}, kernel)),
                                 ::fixy::Tagged<PublishResult, L1>>);
    static_assert(std::is_same_v<decltype(cache.publish_l2(kNondet, ContentHash{1}, RowHash{0}, kernel)),
                                 ::fixy::Tagged<PublishResult, L2>>);
    static_assert(std::is_same_v<decltype(cache.publish_l3(kNondet, ContentHash{1}, RowHash{0}, kernel)),
                                 ::fixy::Tagged<PublishResult, L3>>);
}

// The levels are provenance, not a residency order: no level stands in for
// another, in either direction.
static void test_no_level_converts_to_another() {
    using AtL1 = ::fixy::Tagged<Found, L1>;
    using AtL2 = ::fixy::Tagged<Found, L2>;
    using AtL3 = ::fixy::Tagged<Found, L3>;
    static_assert(!std::is_convertible_v<AtL1, AtL2> && !std::is_convertible_v<AtL2, AtL1>);
    static_assert(!std::is_convertible_v<AtL2, AtL3> && !std::is_convertible_v<AtL3, AtL2>);
    static_assert(!std::is_convertible_v<AtL1, AtL3> && !std::is_convertible_v<AtL3, AtL1>);
    static_assert(!std::is_constructible_v<AtL1, AtL3>);
    static_assert(!std::is_constructible_v<AtL3, AtL1>);
    // A bare lookup result does not become a level either.
    static_assert(!std::is_constructible_v<AtL1, Found>);
}

// A lookup result is one word, and a level adds no byte to it, so the result
// passes in a register.
static void test_layout_invariant() {
    static_assert(sizeof(Found) == 8);
    static_assert(std::is_trivially_copy_constructible_v<Found> && std::is_trivially_destructible_v<Found>);
    static_assert(sizeof(::fixy::Tagged<Found, L1>) == sizeof(Found));
    static_assert(sizeof(::fixy::Tagged<Found, L2>) == sizeof(Found));
    static_assert(sizeof(::fixy::Tagged<Found, L3>) == sizeof(Found));
}

// A consumer that requires the chip level refuses the other two at compile
// time.
template <typename W>
concept takes_chip_level = std::is_same_v<W, ::fixy::Tagged<Found, L3>>;

static void test_level_gate_at_a_consumer() {
    KernelCache cache{kAlloc};
    static_assert(takes_chip_level<decltype(cache.lookup_l3(kNondet, ContentHash{1}, RowHash{0}))>);
    static_assert(!takes_chip_level<decltype(cache.lookup_l1(kNondet, ContentHash{1}, RowHash{0}))>);
    static_assert(!takes_chip_level<decltype(cache.lookup_l2(kNondet, ContentHash{1}, RowHash{0}))>);
}

static void test_unbacked_levels() {
    Kernels kernels = make_kernel_arena();
    KernelCache cache{kAlloc};
    auto const kernel = make_kernel(kernels);

    static_assert(noexcept(cache.lookup_l2(kNondet, ContentHash{1}, RowHash{0})));
    static_assert(noexcept(cache.lookup_l3(kNondet, ContentHash{1}, RowHash{0})));
    static_assert(noexcept(cache.publish_l2(kNondet, ContentHash{1}, RowHash{0}, kernel)));
    static_assert(noexcept(cache.publish_l3(kNondet, ContentHash{1}, RowHash{0}, kernel)));

    // Neither level has a store, so the lookups miss for every key.
    CRUCIBLE_FATAL_INVARIANT(cache.lookup_l2(kNondet, ContentHash{0xEEEE}, RowHash{0}).into().is_none());
    CRUCIBLE_FATAL_INVARIANT(cache.lookup_l3(kNondet, ContentHash{0xEEEE}, RowHash{0}).into().is_none());

    // The publishes report an error rather than a success marker. A vacuous
    // success would let a caller mistake a missing store for a completed
    // write, so the gap travels on the error channel where it can be branched
    // on.
    auto r2 = cache.publish_l2(kNondet, ContentHash{0xFFFF}, RowHash{0}, kernel).into();
    auto r3 = cache.publish_l3(kNondet, ContentHash{0xFFFF}, RowHash{0}, kernel).into();
    CRUCIBLE_TEST_ASSERT(kTest, is_not_yet_implemented(std::move(r2)));
    CRUCIBLE_TEST_ASSERT(kTest, is_not_yet_implemented(std::move(r3)));

    // A publish into L2 stays invisible to L1 at the same key.
    CRUCIBLE_FATAL_INVARIANT(cache.lookup_l1(kNondet, ContentHash{0xFFFF}, RowHash{0}).into().is_none());
}

static void test_three_level_publish() {
    Kernels kernels = make_kernel_arena();
    KernelCache cache{kAlloc};
    auto const kernel = make_kernel(kernels);

    auto r1 = cache.publish_l1(kNondet, ContentHash{0x1111}, RowHash{0xAAAA}, kernel).into();
    auto r2 = cache.publish_l2(kNondet, ContentHash{0x1111}, RowHash{0xAAAA}, kernel).into();
    auto r3 = cache.publish_l3(kNondet, ContentHash{0x1111}, RowHash{0xAAAA}, kernel).into();
    CRUCIBLE_TEST_ASSERT(kTest, r1.is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, r2.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, r3.is_err());
    CRUCIBLE_FATAL_INVARIANT(is_found(cache.lookup_l1(kNondet, ContentHash{0x1111}, RowHash{0xAAAA}).into(), kernel));
}

static void test_lookup_l1_miss() {
    Kernels kernels = make_kernel_arena();
    KernelCache cache{kAlloc};
    CRUCIBLE_FATAL_INVARIANT(cache.lookup_l1(kNondet, ContentHash{0xDEAD}, RowHash{0xBEEF}).into().is_none());

    // A publish under a different key must not make the original key hit
    // through probe pollution.
    auto const kernel = make_kernel(kernels);
    CRUCIBLE_FATAL_INVARIANT(cache.publish_l1(kNondet, ContentHash{0x1234}, RowHash{0}, kernel).into().is_ok());
    CRUCIBLE_FATAL_INVARIANT(cache.lookup_l1(kNondet, ContentHash{0xDEAD}, RowHash{0xBEEF}).into().is_none());
}

// Re-publishing at the same key overwrites the slot. Kernel-variant rotation
// replaces an older compiled variant in place, so a wrapper that rejected
// re-publication would break it.
static void test_publish_l1_variant_update() {
    Kernels kernels = make_kernel_arena();
    KernelCache cache{kAlloc};
    auto const first = make_kernel(kernels);
    auto const second = make_kernel(kernels);
    CRUCIBLE_FATAL_INVARIANT(cache.publish_l1(kNondet, ContentHash{0xAAAA}, RowHash{0}, first).into().is_ok());
    CRUCIBLE_FATAL_INVARIANT(cache.publish_l1(kNondet, ContentHash{0xAAAA}, RowHash{0}, second).into().is_ok());
    CRUCIBLE_FATAL_INVARIANT(is_found(cache.lookup_l1(kNondet, ContentHash{0xAAAA}, RowHash{0}).into(), second));
}

// One content hash with two row hashes occupies two slots. A wrapper
// simplified to ignore the row hash would collapse them into one.
static void test_publish_l1_row_discrimination() {
    Kernels kernels = make_kernel_arena();
    KernelCache cache{kAlloc};
    auto const of_row_a = make_kernel(kernels);
    auto const of_row_b = make_kernel(kernels);
    CRUCIBLE_FATAL_INVARIANT(cache.publish_l1(kNondet, ContentHash{0x1234}, RowHash{0xAAAA}, of_row_a).into().is_ok());
    CRUCIBLE_FATAL_INVARIANT(cache.publish_l1(kNondet, ContentHash{0x1234}, RowHash{0xBBBB}, of_row_b).into().is_ok());
    CRUCIBLE_FATAL_INVARIANT(is_found(cache.lookup_l1(kNondet, ContentHash{0x1234}, RowHash{0xAAAA}).into(), of_row_a));
    CRUCIBLE_FATAL_INVARIANT(is_found(cache.lookup_l1(kNondet, ContentHash{0x1234}, RowHash{0xBBBB}).into(), of_row_b));
    CRUCIBLE_FATAL_INVARIANT(cache.lookup_l1(kNondet, ContentHash{0x1234}, RowHash{0xCCCC}).into().is_none());
}

int main() {
    test_lookup_l1_round_trip();
    test_each_level_names_its_level();
    test_no_level_converts_to_another();
    test_layout_invariant();
    test_level_gate_at_a_consumer();
    test_unbacked_levels();
    test_three_level_publish();
    test_lookup_l1_miss();
    test_publish_l1_variant_update();
    test_publish_l1_row_discrimination();
    ::fixy::check::pass(kTest, "test_kernel_cache_levels: all tests passed\n");
    return 0;
}
