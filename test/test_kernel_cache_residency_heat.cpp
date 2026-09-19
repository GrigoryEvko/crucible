#include <crucible/MerkleDag.h>
#include <crucible/cipher/ComputationCacheFederation.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/safety/_CipherTier.h>
#include <crucible/safety/ResidencyHeat.h>
#include <crucible/safety/diag/RowHashFold.h>
#include "test_assert.h"

#include <cstdio>
#include <cstdint>
#include <type_traits>
#include <utility>

using crucible::ContentHash;
using crucible::RowHash;
using crucible::KernelCache;
using crucible::CompiledKernel;
using crucible::safety::ResidencyHeat;
using crucible::safety::ResidencyHeatTag_v;

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
    auto pub_result = std::move(pub).consume();
    assert(pub_result.has_value());

    auto raw = cache.lookup(ContentHash{0xAAAA}, RowHash{0});
    auto pinned = cache.lookup_l1(ContentHash{0xAAAA}, RowHash{0});
    CompiledKernel* via_wrapper = std::move(pinned).consume();
    assert(raw == via_wrapper);
    assert(via_wrapper == fk_ptr(&fk));
}

static void test_publish_l1_round_trip() {
    KernelCache cache;
    FakeKernel fk{99};

    auto pub = cache.publish_l1(ContentHash{0xBBBB}, RowHash{0}, fk_ptr(&fk));
    auto result = std::move(pub).consume();
    assert(result.has_value());

    assert(cache.lookup(ContentHash{0xBBBB}, RowHash{0}) == fk_ptr(&fk));
}

static void test_lookup_l1_type_identity() {
    KernelCache cache;
    using Got = decltype(cache.lookup_l1(ContentHash{1}, RowHash{0}));
    using Want = ResidencyHeat<ResidencyHeatTag_v::Hot, CompiledKernel*>;
    static_assert(std::is_same_v<Got, Want>, "lookup_l1 must return ResidencyHeat<Hot, CompiledKernel*>");
    static_assert(Got::tier == ResidencyHeatTag_v::Hot);
}

static void test_lookup_l2_type_identity() {
    KernelCache cache;
    using Got = decltype(cache.lookup_l2(ContentHash{1}, RowHash{0}));
    using Want = ResidencyHeat<ResidencyHeatTag_v::Warm, CompiledKernel*>;
    static_assert(std::is_same_v<Got, Want>, "lookup_l2 must return ResidencyHeat<Warm, CompiledKernel*>");
    static_assert(Got::tier == ResidencyHeatTag_v::Warm);

    // L2 has no backing store, so the miss is unconditional rather than a
    // consequence of this key being absent.
    auto pinned = cache.lookup_l2(ContentHash{0x1234}, RowHash{0});
    CompiledKernel* k = std::move(pinned).consume();
    assert(k == nullptr);
}

static void test_lookup_l3_type_identity() {
    KernelCache cache;
    using Got = decltype(cache.lookup_l3(ContentHash{1}, RowHash{0}));
    using Want = ResidencyHeat<ResidencyHeatTag_v::Cold, CompiledKernel*>;
    static_assert(std::is_same_v<Got, Want>, "lookup_l3 must return ResidencyHeat<Cold, CompiledKernel*>");
    static_assert(Got::tier == ResidencyHeatTag_v::Cold);

    // L3 has no backing store either, so the miss is unconditional.
    auto pinned = cache.lookup_l3(ContentHash{0x1234}, RowHash{0});
    CompiledKernel* k = std::move(pinned).consume();
    assert(k == nullptr);
}

static void test_publish_l_type_identity() {
    KernelCache cache;
    FakeKernel fk{1};

    using GotL1 = decltype(cache.publish_l1(ContentHash{1}, RowHash{0}, fk_ptr(&fk)));
    using GotL2 = decltype(cache.publish_l2(ContentHash{1}, RowHash{0}, fk_ptr(&fk)));
    using GotL3 = decltype(cache.publish_l3(ContentHash{1}, RowHash{0}, fk_ptr(&fk)));

    using ExpectedT = std::expected<void, KernelCache::InsertError>;
    using WantL1 = ResidencyHeat<ResidencyHeatTag_v::Hot, ExpectedT>;
    using WantL2 = ResidencyHeat<ResidencyHeatTag_v::Warm, ExpectedT>;
    using WantL3 = ResidencyHeat<ResidencyHeatTag_v::Cold, ExpectedT>;

    static_assert(std::is_same_v<GotL1, WantL1>);
    static_assert(std::is_same_v<GotL2, WantL2>);
    static_assert(std::is_same_v<GotL3, WantL3>);

    // Drain each result to satisfy [[nodiscard]].
    auto p1 = cache.publish_l1(ContentHash{2}, RowHash{0}, fk_ptr(&fk));
    auto p2 = cache.publish_l2(ContentHash{2}, RowHash{0}, fk_ptr(&fk));
    auto p3 = cache.publish_l3(ContentHash{2}, RowHash{0}, fk_ptr(&fk));
    (void)std::move(p1).consume();
    (void)std::move(p2).consume();
    (void)std::move(p3).consume();
}

static void test_hot_satisfies_weaker_tiers() {
    using Hot = ResidencyHeat<ResidencyHeatTag_v::Hot, CompiledKernel*>;
    static_assert(Hot::satisfies<ResidencyHeatTag_v::Hot>);
    static_assert(Hot::satisfies<ResidencyHeatTag_v::Warm>);
    static_assert(Hot::satisfies<ResidencyHeatTag_v::Cold>);
}

static void test_warm_rejected_at_hot_fence() {
    using Warm = ResidencyHeat<ResidencyHeatTag_v::Warm, CompiledKernel*>;
    static_assert(Warm::satisfies<ResidencyHeatTag_v::Warm>);
    static_assert(Warm::satisfies<ResidencyHeatTag_v::Cold>);
    static_assert(!Warm::satisfies<ResidencyHeatTag_v::Hot>,
                  "Warm does not satisfy Hot. The L1 hot-dispatch admission gate "
                  "rests on that rejection.");
}

static void test_cold_rejected_at_higher_fences() {
    using Cold = ResidencyHeat<ResidencyHeatTag_v::Cold, CompiledKernel*>;
    static_assert(Cold::satisfies<ResidencyHeatTag_v::Cold>);
    static_assert(!Cold::satisfies<ResidencyHeatTag_v::Warm>);
    static_assert(!Cold::satisfies<ResidencyHeatTag_v::Hot>);
}

static void test_relax_to_weaker_tiers() {
    KernelCache cache;
    FakeKernel fk{77};
    auto pub = cache.publish_l1(ContentHash{0xCCCC}, RowHash{0}, fk_ptr(&fk));
    (void)std::move(pub).consume();

    auto hot = cache.lookup_l1(ContentHash{0xCCCC}, RowHash{0});
    auto warm = std::move(hot).relax<ResidencyHeatTag_v::Warm>();
    static_assert(std::is_same_v<decltype(warm), ResidencyHeat<ResidencyHeatTag_v::Warm, CompiledKernel*>>);

    auto cold = std::move(warm).relax<ResidencyHeatTag_v::Cold>();
    static_assert(std::is_same_v<decltype(cold), ResidencyHeat<ResidencyHeatTag_v::Cold, CompiledKernel*>>);

    CompiledKernel* k = std::move(cold).consume();
    assert(k == fk_ptr(&fk));
}

static void test_layout_invariant() {
    static_assert(sizeof(ResidencyHeat<ResidencyHeatTag_v::Hot, CompiledKernel*>) == sizeof(CompiledKernel*));
    static_assert(sizeof(ResidencyHeat<ResidencyHeatTag_v::Warm, CompiledKernel*>) == sizeof(CompiledKernel*));
    static_assert(sizeof(ResidencyHeat<ResidencyHeatTag_v::Cold, CompiledKernel*>) == sizeof(CompiledKernel*));
}

template <typename W>
    requires(W::template satisfies<ResidencyHeatTag_v::Hot>)
static CompiledKernel* hot_dispatch_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

static void test_e2e_hot_dispatch_consumer() {
    KernelCache cache;
    FakeKernel fk{0xCAFE};
    auto pub = cache.publish_l1(ContentHash{0xDEAD}, RowHash{0xBEEF}, fk_ptr(&fk));
    (void)std::move(pub).consume();

    auto pinned = cache.lookup_l1(ContentHash{0xDEAD}, RowHash{0xBEEF});
    CompiledKernel* k = hot_dispatch_consumer(std::move(pinned));
    assert(k == fk_ptr(&fk));
}

static void test_phase5_stub_semantics() {
    KernelCache cache;
    FakeKernel fk{1};

    static_assert(noexcept(cache.lookup_l2(ContentHash{1}, RowHash{0})));
    static_assert(noexcept(cache.lookup_l3(ContentHash{1}, RowHash{0})));
    static_assert(noexcept(cache.publish_l2(ContentHash{1}, RowHash{0}, fk_ptr(&fk))));
    static_assert(noexcept(cache.publish_l3(ContentHash{1}, RowHash{0}, fk_ptr(&fk))));

    // Neither tier has a store, so the lookups miss for every key.
    auto l2_hit = cache.lookup_l2(ContentHash{0xEEEE}, RowHash{0});
    auto l3_hit = cache.lookup_l3(ContentHash{0xEEEE}, RowHash{0});
    assert(std::move(l2_hit).consume() == nullptr);
    assert(std::move(l3_hit).consume() == nullptr);

    // The publishes report an error rather than a success marker. A vacuous
    // success would let a caller mistake a missing store for a completed
    // write, so the gap travels on the error channel where it can be branched
    // on.
    auto pub_l2 = cache.publish_l2(ContentHash{0xFFFF}, RowHash{0}, fk_ptr(&fk));
    auto pub_l3 = cache.publish_l3(ContentHash{0xFFFF}, RowHash{0}, fk_ptr(&fk));
    auto r2 = std::move(pub_l2).consume();
    auto r3 = std::move(pub_l3).consume();
    assert(!r2.has_value());
    assert(!r3.has_value());
    assert(r2.error() == KernelCache::InsertError::NotYetImplemented);
    assert(r3.error() == KernelCache::InsertError::NotYetImplemented);

    // A publish into L2 stays invisible to L1 at the same key.
    auto check_l1 = cache.lookup_l1(ContentHash{0xFFFF}, RowHash{0});
    assert(std::move(check_l1).consume() == nullptr);
}

static void test_three_level_federation_publish() {
    KernelCache cache;
    FakeKernel fk{0x42};

    auto p_l1 = cache.publish_l1(ContentHash{0x1111}, RowHash{0xAAAA}, fk_ptr(&fk));
    auto p_l2 = cache.publish_l2(ContentHash{0x1111}, RowHash{0xAAAA}, fk_ptr(&fk));
    auto p_l3 = cache.publish_l3(ContentHash{0x1111}, RowHash{0xAAAA}, fk_ptr(&fk));

    static_assert(!std::is_same_v<decltype(p_l1), decltype(p_l2)>);
    static_assert(!std::is_same_v<decltype(p_l2), decltype(p_l3)>);
    static_assert(!std::is_same_v<decltype(p_l1), decltype(p_l3)>);

    auto r1 = std::move(p_l1).consume();
    auto r2 = std::move(p_l2).consume();
    auto r3 = std::move(p_l3).consume();
    assert(r1.has_value());
    assert(!r2.has_value());
    assert(!r3.has_value());
    assert(r2.error() == KernelCache::InsertError::NotYetImplemented);
    assert(r3.error() == KernelCache::InsertError::NotYetImplemented);

    auto l1_hit = cache.lookup_l1(ContentHash{0x1111}, RowHash{0xAAAA});
    assert(std::move(l1_hit).consume() == fk_ptr(&fk));
}

static void test_lookup_l1_miss_returns_nullptr_pinned() {
    KernelCache cache;
    auto miss = cache.lookup_l1(ContentHash{0xDEAD}, RowHash{0xBEEF});
    static_assert(std::is_same_v<decltype(miss), ResidencyHeat<ResidencyHeatTag_v::Hot, CompiledKernel*>>);
    assert(std::move(miss).consume() == nullptr);

    // A publish under a different key must not make the original key hit
    // through probe pollution.
    FakeKernel fk{1};
    auto pub = cache.publish_l1(ContentHash{0x1234}, RowHash{0}, fk_ptr(&fk));
    (void)std::move(pub).consume();

    auto miss2 = cache.lookup_l1(ContentHash{0xDEAD}, RowHash{0xBEEF});
    assert(std::move(miss2).consume() == nullptr);
}

// Re-publishing at the same key overwrites the slot. Kernel-variant rotation
// replaces an older compiled variant in place, so a wrapper that rejected
// re-publication would break it.
static void test_publish_l1_variant_update() {
    KernelCache cache;
    FakeKernel fk_v1{1};
    FakeKernel fk_v2{2};

    auto p1 = cache.publish_l1(ContentHash{0xAAAA}, RowHash{0}, fk_ptr(&fk_v1));
    assert(std::move(p1).consume().has_value());

    auto p2 = cache.publish_l1(ContentHash{0xAAAA}, RowHash{0}, fk_ptr(&fk_v2));
    assert(std::move(p2).consume().has_value());

    auto hit = cache.lookup_l1(ContentHash{0xAAAA}, RowHash{0});
    assert(std::move(hit).consume() == fk_ptr(&fk_v2));
}

// One content hash with two row hashes occupies two slots. A wrapper
// simplified to ignore the row hash would collapse them into one.
static void test_publish_l1_row_discrimination() {
    KernelCache cache;
    FakeKernel fk_row_a{1};
    FakeKernel fk_row_b{2};

    auto pa = cache.publish_l1(ContentHash{0x1234}, RowHash{0xAAAA}, fk_ptr(&fk_row_a));
    auto pb = cache.publish_l1(ContentHash{0x1234}, RowHash{0xBBBB}, fk_ptr(&fk_row_b));
    assert(std::move(pa).consume().has_value());
    assert(std::move(pb).consume().has_value());

    auto hit_a = cache.lookup_l1(ContentHash{0x1234}, RowHash{0xAAAA});
    auto hit_b = cache.lookup_l1(ContentHash{0x1234}, RowHash{0xBBBB});
    assert(std::move(hit_a).consume() == fk_ptr(&fk_row_a));
    assert(std::move(hit_b).consume() == fk_ptr(&fk_row_b));

    auto miss = cache.lookup_l1(ContentHash{0x1234}, RowHash{0xCCCC});
    assert(std::move(miss).consume() == nullptr);
}

// The two wrappers spell their strongest tier the same way, which is what
// makes an accidental fold into one shared template plausible.
static void test_cross_lattice_non_mixing() {
    using crucible::safety::CipherTier;
    using crucible::safety::CipherTierTag_v;

    using RhHot = ResidencyHeat<ResidencyHeatTag_v::Hot, int>;
    using CtHot = CipherTier<CipherTierTag_v::Hot, int>;

    static_assert(!std::is_same_v<RhHot, CtHot>, "ResidencyHeat<Hot, T> and CipherTier<Hot, T> sit on orthogonal "
                                                 "lattices. A Cipher-Hot value does not flow into a "
                                                 "ResidencyHeat-Hot consumer, nor the reverse. This fires when the "
                                                 "two wrappers have been folded into one shared template.");

    static_assert(!std::is_constructible_v<RhHot, CtHot>);
    static_assert(!std::is_constructible_v<CtHot, RhHot>);
}

// Drift attribution labels a residual against a per-tier baseline. The tier
// reads off the type, so the classification costs nothing at run time.
template <typename W>
[[nodiscard]] static constexpr int classify_cache_tier_for_runtime() noexcept {
    if constexpr (W::tier == ResidencyHeatTag_v::Hot) return 1;
    if constexpr (W::tier == ResidencyHeatTag_v::Warm) return 2;
    if constexpr (W::tier == ResidencyHeatTag_v::Cold) return 3;
    return 0;
}

static void test_runtime_cache_tier_classifier() {
    KernelCache cache;
    FakeKernel fk{1};
    auto p = cache.publish_l1(ContentHash{0x9999}, RowHash{0}, fk_ptr(&fk));
    (void)std::move(p).consume();

    auto l1 = cache.lookup_l1(ContentHash{0x9999}, RowHash{0});
    auto l2 = cache.lookup_l2(ContentHash{0x9999}, RowHash{0});
    auto l3 = cache.lookup_l3(ContentHash{0x9999}, RowHash{0});

    static_assert(classify_cache_tier_for_runtime<decltype(l1)>() == 1);
    static_assert(classify_cache_tier_for_runtime<decltype(l2)>() == 2);
    static_assert(classify_cache_tier_for_runtime<decltype(l3)>() == 3);

    (void)std::move(l1).consume();
    (void)std::move(l2).consume();
    (void)std::move(l3).consume();
}

template <typename W, ResidencyHeatTag_v T_target>
concept can_tighten = requires(W&& w) {
    { std::move(w).template relax<T_target>() };
};

static void test_cannot_tighten_to_stronger_tier() {
    using HotT = ResidencyHeat<ResidencyHeatTag_v::Hot, CompiledKernel*>;
    using WarmT = ResidencyHeat<ResidencyHeatTag_v::Warm, CompiledKernel*>;
    using ColdT = ResidencyHeat<ResidencyHeatTag_v::Cold, CompiledKernel*>;

    static_assert(can_tighten<HotT, ResidencyHeatTag_v::Warm>);
    static_assert(can_tighten<HotT, ResidencyHeatTag_v::Cold>);
    static_assert(can_tighten<WarmT, ResidencyHeatTag_v::Cold>);

    // Relaxing to the tier already held is admissible by reflexivity.
    static_assert(can_tighten<HotT, ResidencyHeatTag_v::Hot>);
    static_assert(can_tighten<WarmT, ResidencyHeatTag_v::Warm>);
    static_assert(can_tighten<ColdT, ResidencyHeatTag_v::Cold>);

    static_assert(!can_tighten<WarmT, ResidencyHeatTag_v::Hot>);
    static_assert(!can_tighten<ColdT, ResidencyHeatTag_v::Warm>);
    static_assert(!can_tighten<ColdT, ResidencyHeatTag_v::Hot>);
}

// L2 carries no store, so what is under test here is only that the row hash
// reaches the API unchanged. A store can then be added without moving the
// parameter.
static void test_l2_row_hash_plumbing_FOUND_I06() {
    KernelCache cache;
    FakeKernel fk{1};

    // The row values span one bit, a byte boundary, an alternating pattern, a
    // value that fits 32 bits and the all-ones saturation. A row hash that was
    // truncated or that special-cased zero would show up against them.
    constexpr RowHash diverse_rows[] = {
        RowHash{0x0001}, RowHash{0x00FF}, RowHash{0xAAAA}, RowHash{0xDEADBEEF}, RowHash{0xFFFFFFFFFFFFFFFFULL},
    };
    for (auto rh : diverse_rows) {
        auto pinned = cache.lookup_l2(ContentHash{0x10000}, rh);
        static_assert(std::is_same_v<decltype(pinned), ResidencyHeat<ResidencyHeatTag_v::Warm, CompiledKernel*>>);
        assert(std::move(pinned).consume() == nullptr);
    }

    for (auto rh : diverse_rows) {
        auto pub = cache.publish_l2(ContentHash{0x20000}, rh, fk_ptr(&fk));
        auto r = std::move(pub).consume();
        assert(!r.has_value());
        assert(r.error() == KernelCache::InsertError::NotYetImplemented);
    }

    // L2 is a separate store, not a fallback: a live L1 entry stays invisible
    // to lookup_l2 at the same key.
    auto pub_l1 = cache.publish_l1(ContentHash{0x30000}, RowHash{0xCAFE}, fk_ptr(&fk));
    (void)std::move(pub_l1).consume();
    auto l1_hit = cache.lookup_l1(ContentHash{0x30000}, RowHash{0xCAFE});
    assert(std::move(l1_hit).consume() == fk_ptr(&fk));
    auto l2_miss = cache.lookup_l2(ContentHash{0x30000}, RowHash{0xCAFE});
    assert(std::move(l2_miss).consume() == nullptr);
}

static void test_l3_row_hash_plumbing_FOUND_I07() {
    KernelCache cache;
    FakeKernel fk{1};

    constexpr RowHash diverse_rows[] = {
        RowHash{0x0001}, RowHash{0x00FF}, RowHash{0xAAAA}, RowHash{0xDEADBEEF}, RowHash{0xFFFFFFFFFFFFFFFFULL},
    };

    for (auto rh : diverse_rows) {
        auto pinned = cache.lookup_l3(ContentHash{0x40000}, rh);
        static_assert(std::is_same_v<decltype(pinned), ResidencyHeat<ResidencyHeatTag_v::Cold, CompiledKernel*>>);
        assert(std::move(pinned).consume() == nullptr);
    }

    for (auto rh : diverse_rows) {
        auto pub = cache.publish_l3(ContentHash{0x50000}, rh, fk_ptr(&fk));
        auto r = std::move(pub).consume();
        assert(!r.has_value());
        assert(r.error() == KernelCache::InsertError::NotYetImplemented);
    }

    // L3 is likewise a separate store from L1.
    auto pub_l1 = cache.publish_l1(ContentHash{0x60000}, RowHash{0xBABE}, fk_ptr(&fk));
    (void)std::move(pub_l1).consume();
    auto l1_hit = cache.lookup_l1(ContentHash{0x60000}, RowHash{0xBABE});
    assert(std::move(l1_hit).consume() == fk_ptr(&fk));
    auto l3_miss = cache.lookup_l3(ContentHash{0x60000}, RowHash{0xBABE});
    assert(std::move(l3_miss).consume() == nullptr);
}

// Guards against a single backing store shared by both tiers. Neither tier has
// a store, so both answer nullptr for every key and these assertions hold
// vacuously.
static void test_l2_l3_cross_tier_isolation_FOUND_I06_I07_AUDIT() {
    KernelCache cache;
    FakeKernel fk_l2_only{42};
    FakeKernel fk_l3_only{43};

    auto p2 = cache.publish_l2(ContentHash{0x70000}, RowHash{0xD00D}, fk_ptr(&fk_l2_only));
    (void)std::move(p2).consume();

    auto l3_miss = cache.lookup_l3(ContentHash{0x70000}, RowHash{0xD00D});
    assert(std::move(l3_miss).consume() == nullptr);

    auto p3 = cache.publish_l3(ContentHash{0x80000}, RowHash{0xF00D}, fk_ptr(&fk_l3_only));
    (void)std::move(p3).consume();

    auto l2_miss = cache.lookup_l2(ContentHash{0x80000}, RowHash{0xF00D});
    assert(std::move(l2_miss).consume() == nullptr);

    // Both tiers take a publish at one shared key. Each side must answer with
    // its own kernel, never the other side's.
    auto p2_shared = cache.publish_l2(ContentHash{0x90000}, RowHash{0xCEED}, fk_ptr(&fk_l2_only));
    auto p3_shared = cache.publish_l3(ContentHash{0x90000}, RowHash{0xCEED}, fk_ptr(&fk_l3_only));
    (void)std::move(p2_shared).consume();
    (void)std::move(p3_shared).consume();

    auto l2_shared = cache.lookup_l2(ContentHash{0x90000}, RowHash{0xCEED});
    auto l3_shared = cache.lookup_l3(ContentHash{0x90000}, RowHash{0xCEED});
    assert(std::move(l2_shared).consume() == nullptr);
    assert(std::move(l3_shared).consume() == nullptr);
}

// Every row hash above is a hand-written literal. Here it comes from the row
// projection instead, so a change to the fold that broke the cache
// integration has somewhere to surface.

namespace test_i18 {
namespace eff = ::crucible::effects;
namespace fed = ::crucible::cipher::federation;

inline void f_unary(int) noexcept {}
inline void f_binary(int, double) noexcept {}

using R0 = eff::Row<>;
using RBg = eff::Row<eff::Effect::Bg>;
using RIO = eff::Row<eff::Effect::IO>;
using RBgIO = eff::Row<eff::Effect::Bg, eff::Effect::IO>;
using RIOBg = eff::Row<eff::Effect::IO, eff::Effect::Bg>;
}  // namespace test_i18

static void test_publish_row_validated_FOUND_I18() {
    namespace eff = ::test_i18;
    namespace fed = ::crucible::cipher::federation;
    namespace diag = ::crucible::safety::diag;

    static_assert(diag::row_hash_contribution_v<eff::R0> != 0u,
                  "The empty row projects to a non-zero row hash. Cache slot identity "
                  "rests on that, since the seed carries the row cardinality.");
    static_assert(diag::row_hash_contribution_v<eff::RBg> != 0u);
    static_assert(diag::row_hash_contribution_v<eff::RIO> != 0u);
    static_assert(diag::row_hash_contribution_v<eff::RBgIO> != 0u);

    KernelCache cache;
    FakeKernel fk_a{0xA1};
    FakeKernel fk_b{0xB2};

    constexpr ContentHash ch_a{0xAAAA0001};
    constexpr ContentHash ch_b{0xBBBB0002};

    static_assert(fed::federation_row_hash<eff::R0>().raw() == diag::row_hash_contribution_v<eff::R0>);
    static_assert(fed::federation_row_hash<eff::RBg>().raw() == diag::row_hash_contribution_v<eff::RBg>);

    const RowHash rh_empty = fed::federation_row_hash<eff::R0>();
    const RowHash rh_bg = fed::federation_row_hash<eff::RBg>();
    const RowHash rh_io = fed::federation_row_hash<eff::RIO>();
    const RowHash rh_bgio = fed::federation_row_hash<eff::RBgIO>();
    const RowHash rh_iobg = fed::federation_row_hash<eff::RIOBg>();

    {
        auto pre = cache.lookup_l1(ch_a, rh_bg);
        assert(std::move(pre).consume() == nullptr);
    }

    {
        auto pub = cache.publish_l1(ch_a, rh_bg, fk_ptr(&fk_a));
        auto r = std::move(pub).consume();
        assert(r.has_value());

        auto post = cache.lookup_l1(ch_a, rh_bg);
        assert(std::move(post).consume() == fk_ptr(&fk_a));
    }

    {
        assert(rh_empty != rh_bg);
        assert(rh_bg != rh_io);
        assert(rh_io != rh_bgio);

        auto miss_at_empty = cache.lookup_l1(ch_a, rh_empty);
        assert(std::move(miss_at_empty).consume() == nullptr);
        auto miss_at_io = cache.lookup_l1(ch_a, rh_io);
        assert(std::move(miss_at_io).consume() == nullptr);
    }

    // The projection sorts its atoms, so two permutations of one row share a
    // hash. The cache keys on the hash bytes rather than on the row type, so
    // that semantic equivalence becomes slot identity.
    {
        assert(rh_bgio == rh_iobg);

        auto pub = cache.publish_l1(ch_b, rh_bgio, fk_ptr(&fk_b));
        auto r = std::move(pub).consume();
        assert(r.has_value());

        auto via_iobg = cache.lookup_l1(ch_b, rh_iobg);
        assert(std::move(via_iobg).consume() == fk_ptr(&fk_b));
    }

    // The storeless tiers accept a projected row hash on the same terms as a
    // literal one.
    {
        auto l2_miss = cache.lookup_l2(ch_a, rh_bg);
        assert(std::move(l2_miss).consume() == nullptr);

        auto l3_miss = cache.lookup_l3(ch_a, rh_bg);
        assert(std::move(l3_miss).consume() == nullptr);

        auto p2 = cache.publish_l2(ch_a, rh_bg, fk_ptr(&fk_a));
        auto r2 = std::move(p2).consume();
        assert(!r2.has_value());
        assert(r2.error() == KernelCache::InsertError::NotYetImplemented);

        auto p3 = cache.publish_l3(ch_a, rh_bg, fk_ptr(&fk_a));
        auto r3 = std::move(p3).consume();
        assert(!r3.has_value());
        assert(r3.error() == KernelCache::InsertError::NotYetImplemented);
    }
}

static void test_publish_row_validated_FOUND_I18_AUDIT() {
    namespace eff = ::test_i18;
    namespace fed = ::crucible::cipher::federation;
    namespace diag = ::crucible::safety::diag;
    using crucible::cipher::computation_cache_key_in_row;

    using RFull = ::crucible::effects::Row<::crucible::effects::Effect::Alloc, ::crucible::effects::Effect::IO,
                                           ::crucible::effects::Effect::Block, ::crucible::effects::Effect::Bg,
                                           ::crucible::effects::Effect::Init, ::crucible::effects::Effect::Test>;

    {
        KernelCache cache;
        constexpr ContentHash ch{0xCAFE0001};

        const RowHash rows[] = {
            fed::federation_row_hash<eff::R0>(),  fed::federation_row_hash<eff::RBg>(),
            fed::federation_row_hash<eff::RIO>(), fed::federation_row_hash<eff::RBgIO>(),
            fed::federation_row_hash<RFull>(),
        };
        for (auto rh : rows) {
            auto miss = cache.lookup_l1(ch, rh);
            assert(std::move(miss).consume() == nullptr);
        }

        for (std::size_t i = 0; i < 5; ++i) {
            for (std::size_t j = i + 1; j < 5; ++j) {
                assert(rows[i] != rows[j]);
            }
        }
    }

    // Variant update behaves the same when the row hash comes from the
    // projection rather than from a literal.
    {
        KernelCache cache;
        FakeKernel fk_a{0xA1};
        FakeKernel fk_b{0xB2};
        constexpr ContentHash ch{0xDEAD0002};
        const RowHash rh_bg = fed::federation_row_hash<eff::RBg>();

        auto pa = cache.publish_l1(ch, rh_bg, fk_ptr(&fk_a));
        assert(std::move(pa).consume().has_value());
        auto hit_a = cache.lookup_l1(ch, rh_bg);
        assert(std::move(hit_a).consume() == fk_ptr(&fk_a));

        auto pb = cache.publish_l1(ch, rh_bg, fk_ptr(&fk_b));
        assert(std::move(pb).consume().has_value());
        auto hit_b = cache.lookup_l1(ch, rh_bg);
        assert(std::move(hit_b).consume() == fk_ptr(&fk_b));
    }

    // The raw computation cache key is a valid ContentHash with no narrowing
    // or further transformation, so a call site can build the key itself and
    // still land in the slot the federation helpers reach.
    {
        KernelCache cache;
        FakeKernel fk{0xC3};

        constexpr auto f11_key = computation_cache_key_in_row<&eff::f_unary, eff::RBg, int>;
        const ContentHash ch_from_f11{f11_key};
        const RowHash rh = RowHash{diag::row_hash_contribution_v<eff::RBg>};

        const ContentHash ch_from_f12 = fed::federation_content_hash<&eff::f_unary, eff::RBg, int>();
        const RowHash rh_from_f12 = fed::federation_row_hash<eff::RBg>();
        assert(ch_from_f11 == ch_from_f12);
        assert(rh == rh_from_f12);

        auto pub = cache.publish_l1(ch_from_f11, rh, fk_ptr(&fk));
        assert(std::move(pub).consume().has_value());

        auto via_f12 = cache.lookup_l1(ch_from_f12, rh_from_f12);
        assert(std::move(via_f12).consume() == fk_ptr(&fk));

        auto via_f11 = cache.lookup_l1(ch_from_f11, rh);
        assert(std::move(via_f11).consume() == fk_ptr(&fk));
    }

    // RFull carries every effect atom, so it is the saturation case for the
    // projection.
    {
        KernelCache cache;
        FakeKernel fk{0xD4};
        constexpr ContentHash ch{0xFEED0003};

        const RowHash rh_full = fed::federation_row_hash<RFull>();
        const RowHash rh_empty = fed::federation_row_hash<eff::R0>();
        const RowHash rh_bg = fed::federation_row_hash<eff::RBg>();
        const RowHash rh_bgio = fed::federation_row_hash<eff::RBgIO>();

        assert(rh_full != rh_empty);
        assert(rh_full != rh_bg);
        assert(rh_full != rh_bgio);

        auto pub = cache.publish_l1(ch, rh_full, fk_ptr(&fk));
        assert(std::move(pub).consume().has_value());
        auto hit = cache.lookup_l1(ch, rh_full);
        assert(std::move(hit).consume() == fk_ptr(&fk));

        auto miss_at_empty = cache.lookup_l1(ch, rh_empty);
        assert(std::move(miss_at_empty).consume() == nullptr);
        auto miss_at_bg = cache.lookup_l1(ch, rh_bg);
        assert(std::move(miss_at_bg).consume() == nullptr);
        auto miss_at_bgio = cache.lookup_l1(ch, rh_bgio);
        assert(std::move(miss_at_bgio).consume() == nullptr);
    }

    // Two content hashes crossed with two rows give four occupied slots, each
    // holding a kernel of its own.
    {
        KernelCache cache;
        FakeKernel fk_00{0xE5};
        FakeKernel fk_01{0xE6};
        FakeKernel fk_10{0xE7};
        FakeKernel fk_11{0xE8};

        constexpr ContentHash ch_a{0xAAAA0004};
        constexpr ContentHash ch_b{0xBBBB0004};
        const RowHash rh_0 = fed::federation_row_hash<eff::R0>();
        const RowHash rh_1 = fed::federation_row_hash<eff::RBg>();

        assert(ch_a != ch_b);
        assert(rh_0 != rh_1);

        (void)std::move(cache.publish_l1(ch_a, rh_0, fk_ptr(&fk_00))).consume();
        (void)std::move(cache.publish_l1(ch_a, rh_1, fk_ptr(&fk_01))).consume();
        (void)std::move(cache.publish_l1(ch_b, rh_0, fk_ptr(&fk_10))).consume();
        (void)std::move(cache.publish_l1(ch_b, rh_1, fk_ptr(&fk_11))).consume();

        assert(std::move(cache.lookup_l1(ch_a, rh_0)).consume() == fk_ptr(&fk_00));
        assert(std::move(cache.lookup_l1(ch_a, rh_1)).consume() == fk_ptr(&fk_01));
        assert(std::move(cache.lookup_l1(ch_b, rh_0)).consume() == fk_ptr(&fk_10));
        assert(std::move(cache.lookup_l1(ch_b, rh_1)).consume() == fk_ptr(&fk_11));

        // Four keys outside the published set: an unpublished content hash
        // against each published row, and each published content hash against
        // an unpublished row.
        const ContentHash ch_c{0xCCCC0005};
        const RowHash rh_2 = fed::federation_row_hash<eff::RIO>();

        assert(std::move(cache.lookup_l1(ch_c, rh_0)).consume() == nullptr);
        assert(std::move(cache.lookup_l1(ch_c, rh_1)).consume() == nullptr);
        assert(std::move(cache.lookup_l1(ch_a, rh_2)).consume() == nullptr);
        assert(std::move(cache.lookup_l1(ch_b, rh_2)).consume() == nullptr);
    }
}

int main() {
    test_lookup_l1_round_trip();
    test_publish_l1_round_trip();
    test_lookup_l1_type_identity();
    test_lookup_l2_type_identity();
    test_lookup_l3_type_identity();
    test_publish_l_type_identity();
    test_hot_satisfies_weaker_tiers();
    test_warm_rejected_at_hot_fence();
    test_cold_rejected_at_higher_fences();
    test_relax_to_weaker_tiers();
    test_layout_invariant();
    test_e2e_hot_dispatch_consumer();
    test_phase5_stub_semantics();
    test_three_level_federation_publish();
    test_lookup_l1_miss_returns_nullptr_pinned();
    test_publish_l1_variant_update();
    test_publish_l1_row_discrimination();
    test_cross_lattice_non_mixing();
    test_runtime_cache_tier_classifier();
    test_cannot_tighten_to_stronger_tier();
    test_l2_row_hash_plumbing_FOUND_I06();
    test_l3_row_hash_plumbing_FOUND_I07();
    test_l2_l3_cross_tier_isolation_FOUND_I06_I07_AUDIT();
    test_publish_row_validated_FOUND_I18();
    test_publish_row_validated_FOUND_I18_AUDIT();

    std::puts("ok");
    return 0;
}
