// The three levels of the kernel cache: L1 holds the vendor-neutral form,
// L2 the form for one vendor family, L3 the compiled bytes for one chip.
// Each level tags what it returns with the level as its provenance, so a
// value from one level is a different type from a value from another.

#include <crucible/MerkleDag.h>
#include <fixy/Tagged.h>
#include "test_assert.h"

#include <cstdint>
#include <cstdio>
#include <expected>
#include <type_traits>
#include <utility>

using crucible::CompiledKernel;
using crucible::ContentHash;
using crucible::KernelCache;
using crucible::RowHash;

using L1 = KernelCache::VendorNeutralLevel;
using L2 = KernelCache::VendorFamilyLevel;
using L3 = KernelCache::ChipLevel;
using PublishResult = std::expected<void, KernelCache::InsertError>;

struct FakeKernel {
    int x;
};

// The cache never dereferences the kernel pointer, so an unrelated allocation
// supplies the stable identity the test compares against.
static CompiledKernel* fk_ptr(FakeKernel* fk) noexcept { return static_cast<CompiledKernel*>(static_cast<void*>(fk)); }

static void test_lookup_l1_round_trip() {
    KernelCache cache;
    FakeKernel fk{42};
    auto pub = cache.publish_l1(ContentHash{0xAAAA}, RowHash{0}, fk_ptr(&fk));
    assert(std::move(pub).into().has_value());

    auto raw = cache.lookup(ContentHash{0xAAAA}, RowHash{0});
    auto tagged = cache.lookup_l1(ContentHash{0xAAAA}, RowHash{0});
    CompiledKernel* via_level = std::move(tagged).into();
    assert(raw == via_level);
    assert(via_level == fk_ptr(&fk));
}

static void test_each_level_names_its_level() {
    KernelCache cache;
    FakeKernel fk{1};
    static_assert(std::is_same_v<decltype(cache.lookup_l1(ContentHash{1}, RowHash{0})),
                                 ::fixy::Tagged<CompiledKernel*, L1>>);
    static_assert(std::is_same_v<decltype(cache.lookup_l2(ContentHash{1}, RowHash{0})),
                                 ::fixy::Tagged<CompiledKernel*, L2>>);
    static_assert(std::is_same_v<decltype(cache.lookup_l3(ContentHash{1}, RowHash{0})),
                                 ::fixy::Tagged<CompiledKernel*, L3>>);
    static_assert(std::is_same_v<decltype(cache.publish_l1(ContentHash{1}, RowHash{0}, fk_ptr(&fk))),
                                 ::fixy::Tagged<PublishResult, L1>>);
    static_assert(std::is_same_v<decltype(cache.publish_l2(ContentHash{1}, RowHash{0}, fk_ptr(&fk))),
                                 ::fixy::Tagged<PublishResult, L2>>);
    static_assert(std::is_same_v<decltype(cache.publish_l3(ContentHash{1}, RowHash{0}, fk_ptr(&fk))),
                                 ::fixy::Tagged<PublishResult, L3>>);
}

// The levels are provenance, not a residency order: no level stands in for
// another, in either direction.
static void test_no_level_converts_to_another() {
    using AtL1 = ::fixy::Tagged<CompiledKernel*, L1>;
    using AtL2 = ::fixy::Tagged<CompiledKernel*, L2>;
    using AtL3 = ::fixy::Tagged<CompiledKernel*, L3>;
    static_assert(!std::is_convertible_v<AtL1, AtL2> && !std::is_convertible_v<AtL2, AtL1>);
    static_assert(!std::is_convertible_v<AtL2, AtL3> && !std::is_convertible_v<AtL3, AtL2>);
    static_assert(!std::is_convertible_v<AtL1, AtL3> && !std::is_convertible_v<AtL3, AtL1>);
    static_assert(!std::is_constructible_v<AtL1, AtL3>);
    static_assert(!std::is_constructible_v<AtL3, AtL1>);
    // A bare pointer does not become a level either.
    static_assert(!std::is_constructible_v<AtL1, CompiledKernel*>);
}

static void test_layout_invariant() {
    static_assert(sizeof(::fixy::Tagged<CompiledKernel*, L1>) == sizeof(CompiledKernel*));
    static_assert(sizeof(::fixy::Tagged<CompiledKernel*, L2>) == sizeof(CompiledKernel*));
    static_assert(sizeof(::fixy::Tagged<CompiledKernel*, L3>) == sizeof(CompiledKernel*));
}

// A consumer that requires the chip level refuses the other two at compile
// time.
template <typename W>
concept takes_chip_level = std::is_same_v<W, ::fixy::Tagged<CompiledKernel*, L3>>;

static void test_level_gate_at_a_consumer() {
    KernelCache cache;
    static_assert(takes_chip_level<decltype(cache.lookup_l3(ContentHash{1}, RowHash{0}))>);
    static_assert(!takes_chip_level<decltype(cache.lookup_l1(ContentHash{1}, RowHash{0}))>);
    static_assert(!takes_chip_level<decltype(cache.lookup_l2(ContentHash{1}, RowHash{0}))>);
}

static void test_unbacked_levels() {
    KernelCache cache;
    FakeKernel fk{1};

    static_assert(noexcept(cache.lookup_l2(ContentHash{1}, RowHash{0})));
    static_assert(noexcept(cache.lookup_l3(ContentHash{1}, RowHash{0})));
    static_assert(noexcept(cache.publish_l2(ContentHash{1}, RowHash{0}, fk_ptr(&fk))));
    static_assert(noexcept(cache.publish_l3(ContentHash{1}, RowHash{0}, fk_ptr(&fk))));

    // Neither level has a store, so the lookups miss for every key.
    assert(cache.lookup_l2(ContentHash{0xEEEE}, RowHash{0}).into() == nullptr);
    assert(cache.lookup_l3(ContentHash{0xEEEE}, RowHash{0}).into() == nullptr);

    // The publishes report an error rather than a success marker. A vacuous
    // success would let a caller mistake a missing store for a completed
    // write, so the gap travels on the error channel where it can be branched
    // on.
    auto r2 = cache.publish_l2(ContentHash{0xFFFF}, RowHash{0}, fk_ptr(&fk)).into();
    auto r3 = cache.publish_l3(ContentHash{0xFFFF}, RowHash{0}, fk_ptr(&fk)).into();
    assert(!r2.has_value() && r2.error() == KernelCache::InsertError::NotYetImplemented);
    assert(!r3.has_value() && r3.error() == KernelCache::InsertError::NotYetImplemented);

    // A publish into L2 stays invisible to L1 at the same key.
    assert(cache.lookup_l1(ContentHash{0xFFFF}, RowHash{0}).into() == nullptr);
}

static void test_three_level_publish() {
    KernelCache cache;
    FakeKernel fk{0x42};

    auto r1 = cache.publish_l1(ContentHash{0x1111}, RowHash{0xAAAA}, fk_ptr(&fk)).into();
    auto r2 = cache.publish_l2(ContentHash{0x1111}, RowHash{0xAAAA}, fk_ptr(&fk)).into();
    auto r3 = cache.publish_l3(ContentHash{0x1111}, RowHash{0xAAAA}, fk_ptr(&fk)).into();
    assert(r1.has_value());
    assert(!r2.has_value());
    assert(!r3.has_value());
    assert(cache.lookup_l1(ContentHash{0x1111}, RowHash{0xAAAA}).into() == fk_ptr(&fk));
}

static void test_lookup_l1_miss() {
    KernelCache cache;
    assert(cache.lookup_l1(ContentHash{0xDEAD}, RowHash{0xBEEF}).into() == nullptr);

    // A publish under a different key must not make the original key hit
    // through probe pollution.
    FakeKernel fk{1};
    assert(cache.publish_l1(ContentHash{0x1234}, RowHash{0}, fk_ptr(&fk)).into().has_value());
    assert(cache.lookup_l1(ContentHash{0xDEAD}, RowHash{0xBEEF}).into() == nullptr);
}

// Re-publishing at the same key overwrites the slot. Kernel-variant rotation
// replaces an older compiled variant in place, so a wrapper that rejected
// re-publication would break it.
static void test_publish_l1_variant_update() {
    KernelCache cache;
    FakeKernel fk_v1{1};
    FakeKernel fk_v2{2};
    assert(cache.publish_l1(ContentHash{0xAAAA}, RowHash{0}, fk_ptr(&fk_v1)).into().has_value());
    assert(cache.publish_l1(ContentHash{0xAAAA}, RowHash{0}, fk_ptr(&fk_v2)).into().has_value());
    assert(cache.lookup_l1(ContentHash{0xAAAA}, RowHash{0}).into() == fk_ptr(&fk_v2));
}

// One content hash with two row hashes occupies two slots. A wrapper
// simplified to ignore the row hash would collapse them into one.
static void test_publish_l1_row_discrimination() {
    KernelCache cache;
    FakeKernel fk_row_a{1};
    FakeKernel fk_row_b{2};
    assert(cache.publish_l1(ContentHash{0x1234}, RowHash{0xAAAA}, fk_ptr(&fk_row_a)).into().has_value());
    assert(cache.publish_l1(ContentHash{0x1234}, RowHash{0xBBBB}, fk_ptr(&fk_row_b)).into().has_value());
    assert(cache.lookup_l1(ContentHash{0x1234}, RowHash{0xAAAA}).into() == fk_ptr(&fk_row_a));
    assert(cache.lookup_l1(ContentHash{0x1234}, RowHash{0xBBBB}).into() == fk_ptr(&fk_row_b));
    assert(cache.lookup_l1(ContentHash{0x1234}, RowHash{0xCCCC}).into() == nullptr);
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
    std::printf("test_kernel_cache_levels: all tests passed\n");
    return 0;
}
