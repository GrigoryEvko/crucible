// FOUND-G52 — KernelCache ResidencyHeat-pinned three-level cache surface.
//
// Verifies the lookup_l1/l2/l3 + publish_l1/l2/l3 variants added to
// KernelCache.  These are the production call sites for the
// ResidencyHeat wrapper (FOUND-G49 substrate, FOUND-G51 negative-
// compile fixtures).
//
//   lookup_l1 / publish_l1  → ResidencyHeat<Hot,  T>  — REAL today
//                              (wraps existing single-tier cache)
//   lookup_l2 / publish_l2  → ResidencyHeat<Warm, T>  — Phase 5 stub
//                              (per-vendor-family federation)
//   lookup_l3 / publish_l3  → ResidencyHeat<Cold, T>  — Phase 5 stub
//                              (per-chip compiled-bytes archive)
//
// Test surface coverage (T01-T13):
//   T01 — lookup_l1 round-trip vs raw lookup
//   T02 — publish_l1 round-trip vs raw insert
//   T03 — lookup_l1 type-identity (Hot-pinned)
//   T04 — lookup_l2 type-identity (Warm-pinned, stub returns nullptr)
//   T05 — lookup_l3 type-identity (Cold-pinned, stub returns nullptr)
//   T06 — publish_l{1,2,3} type-identity (returns ResidencyHeat-
//          pinned expected<void, InsertError>)
//   T07 — fence-acceptance: Hot subsumes Warm and Cold
//   T08 — fence-acceptance: Warm rejected at Hot fence
//   T09 — fence-acceptance: Cold rejected at higher fences
//   T10 — relax DOWN-the-lattice (Hot → Warm → Cold)
//   T11 — layout invariant (sizeof preservation)
//   T12 — end-to-end Hot-fence consumer
//   T13 — phase-5-stub semantics (l2/l3 lookups always miss; l2/l3
//          publishes return success-marker but don't persist)

#include <crucible/MerkleDag.h>
#include <crucible/safety/CipherTier.h>
#include <crucible/safety/ResidencyHeat.h>
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

// Stand-in for the real CompiledKernel (test-local type punning).
struct FakeKernel { int x; };

// Reinterpret a FakeKernel* as a CompiledKernel*.  Mirrors the
// pattern in test_merkle_dag.cpp — the cache treats CompiledKernel*
// opaquely.
static CompiledKernel* fk_ptr(FakeKernel* fk) noexcept {
    return reinterpret_cast<CompiledKernel*>(fk);
}

// ── T01 — lookup_l1 round-trip vs raw lookup ────────────────────
static void test_lookup_l1_round_trip() {
    KernelCache cache;
    FakeKernel fk{42};
    auto pub = cache.publish_l1(ContentHash{0xAAAA}, RowHash{0}, fk_ptr(&fk));
    auto pub_result = std::move(pub).consume();
    assert(pub_result.has_value());

    // lookup_l1 must find the kernel, value bytes match raw lookup.
    auto raw = cache.lookup(ContentHash{0xAAAA}, RowHash{0});
    auto pinned = cache.lookup_l1(ContentHash{0xAAAA}, RowHash{0});
    CompiledKernel* via_wrapper = std::move(pinned).consume();
    assert(raw == via_wrapper);
    assert(via_wrapper == fk_ptr(&fk));
}

// ── T02 — publish_l1 round-trip vs raw insert ───────────────────
static void test_publish_l1_round_trip() {
    KernelCache cache;
    FakeKernel fk{99};

    // publish_l1 wraps insert(); raw insert() at the same key
    // performs a variant-update (overwrite-same-slot).
    auto pub = cache.publish_l1(ContentHash{0xBBBB}, RowHash{0}, fk_ptr(&fk));
    auto result = std::move(pub).consume();
    assert(result.has_value());

    // lookup must find what publish_l1 wrote.
    assert(cache.lookup(ContentHash{0xBBBB}, RowHash{0}) == fk_ptr(&fk));
}

// ── T03 — lookup_l1 type-identity ───────────────────────────────
static void test_lookup_l1_type_identity() {
    KernelCache cache;
    using Got  = decltype(cache.lookup_l1(ContentHash{1}, RowHash{0}));
    using Want = ResidencyHeat<ResidencyHeatTag_v::Hot, CompiledKernel*>;
    static_assert(std::is_same_v<Got, Want>,
        "lookup_l1 must return ResidencyHeat<Hot, CompiledKernel*>");
    static_assert(Got::tier == ResidencyHeatTag_v::Hot);
}

// ── T04 — lookup_l2 type-identity (Phase 5 stub) ────────────────
static void test_lookup_l2_type_identity() {
    KernelCache cache;
    using Got  = decltype(cache.lookup_l2(ContentHash{1}, RowHash{0}));
    using Want = ResidencyHeat<ResidencyHeatTag_v::Warm, CompiledKernel*>;
    static_assert(std::is_same_v<Got, Want>,
        "lookup_l2 must return ResidencyHeat<Warm, CompiledKernel*>");
    static_assert(Got::tier == ResidencyHeatTag_v::Warm);

    // Phase 5 stub: always misses.
    auto pinned = cache.lookup_l2(ContentHash{0x1234}, RowHash{0});
    CompiledKernel* k = std::move(pinned).consume();
    assert(k == nullptr);
}

// ── T05 — lookup_l3 type-identity (Phase 5 stub) ────────────────
static void test_lookup_l3_type_identity() {
    KernelCache cache;
    using Got  = decltype(cache.lookup_l3(ContentHash{1}, RowHash{0}));
    using Want = ResidencyHeat<ResidencyHeatTag_v::Cold, CompiledKernel*>;
    static_assert(std::is_same_v<Got, Want>,
        "lookup_l3 must return ResidencyHeat<Cold, CompiledKernel*>");
    static_assert(Got::tier == ResidencyHeatTag_v::Cold);

    // Phase 5 stub: always misses.
    auto pinned = cache.lookup_l3(ContentHash{0x1234}, RowHash{0});
    CompiledKernel* k = std::move(pinned).consume();
    assert(k == nullptr);
}

// ── T06 — publish_l{1,2,3} type-identity ────────────────────────
static void test_publish_l_type_identity() {
    KernelCache cache;
    FakeKernel fk{1};

    using GotL1 = decltype(cache.publish_l1(ContentHash{1}, RowHash{0}, fk_ptr(&fk)));
    using GotL2 = decltype(cache.publish_l2(ContentHash{1}, RowHash{0}, fk_ptr(&fk)));
    using GotL3 = decltype(cache.publish_l3(ContentHash{1}, RowHash{0}, fk_ptr(&fk)));

    using ExpectedT = std::expected<void, KernelCache::InsertError>;
    using WantL1 = ResidencyHeat<ResidencyHeatTag_v::Hot,  ExpectedT>;
    using WantL2 = ResidencyHeat<ResidencyHeatTag_v::Warm, ExpectedT>;
    using WantL3 = ResidencyHeat<ResidencyHeatTag_v::Cold, ExpectedT>;

    static_assert(std::is_same_v<GotL1, WantL1>);
    static_assert(std::is_same_v<GotL2, WantL2>);
    static_assert(std::is_same_v<GotL3, WantL3>);

    // Drain so [[nodiscard]] doesn't complain.
    auto p1 = cache.publish_l1(ContentHash{2}, RowHash{0}, fk_ptr(&fk));
    auto p2 = cache.publish_l2(ContentHash{2}, RowHash{0}, fk_ptr(&fk));
    auto p3 = cache.publish_l3(ContentHash{2}, RowHash{0}, fk_ptr(&fk));
    (void)std::move(p1).consume();
    (void)std::move(p2).consume();
    (void)std::move(p3).consume();
}

// ── T07 — Hot subsumes Warm and Cold ────────────────────────────
static void test_hot_satisfies_weaker_tiers() {
    using Hot = ResidencyHeat<ResidencyHeatTag_v::Hot, CompiledKernel*>;
    static_assert( Hot::satisfies<ResidencyHeatTag_v::Hot>);
    static_assert( Hot::satisfies<ResidencyHeatTag_v::Warm>);
    static_assert( Hot::satisfies<ResidencyHeatTag_v::Cold>);
}

// ── T08 — Warm rejected at Hot fence ────────────────────────────
static void test_warm_rejected_at_hot_fence() {
    using Warm = ResidencyHeat<ResidencyHeatTag_v::Warm, CompiledKernel*>;
    static_assert( Warm::satisfies<ResidencyHeatTag_v::Warm>);
    static_assert( Warm::satisfies<ResidencyHeatTag_v::Cold>);
    static_assert(!Warm::satisfies<ResidencyHeatTag_v::Hot>,
        "Warm MUST NOT satisfy Hot — load-bearing rejection for the "
        "L1 hot-dispatch admission gate.");
}

// ── T09 — Cold rejected at Warm/Hot fences ──────────────────────
static void test_cold_rejected_at_higher_fences() {
    using Cold = ResidencyHeat<ResidencyHeatTag_v::Cold, CompiledKernel*>;
    static_assert( Cold::satisfies<ResidencyHeatTag_v::Cold>);
    static_assert(!Cold::satisfies<ResidencyHeatTag_v::Warm>);
    static_assert(!Cold::satisfies<ResidencyHeatTag_v::Hot>);
}

// ── T10 — relax DOWN-the-lattice ────────────────────────────────
static void test_relax_to_weaker_tiers() {
    KernelCache cache;
    FakeKernel fk{77};
    auto pub = cache.publish_l1(ContentHash{0xCCCC}, RowHash{0}, fk_ptr(&fk));
    (void)std::move(pub).consume();

    auto hot = cache.lookup_l1(ContentHash{0xCCCC}, RowHash{0});
    auto warm = std::move(hot).relax<ResidencyHeatTag_v::Warm>();
    static_assert(std::is_same_v<decltype(warm),
        ResidencyHeat<ResidencyHeatTag_v::Warm, CompiledKernel*>>);

    auto cold = std::move(warm).relax<ResidencyHeatTag_v::Cold>();
    static_assert(std::is_same_v<decltype(cold),
        ResidencyHeat<ResidencyHeatTag_v::Cold, CompiledKernel*>>);

    CompiledKernel* k = std::move(cold).consume();
    assert(k == fk_ptr(&fk));
}

// ── T11 — layout invariant ──────────────────────────────────────
static void test_layout_invariant() {
    static_assert(sizeof(ResidencyHeat<ResidencyHeatTag_v::Hot,  CompiledKernel*>)
                  == sizeof(CompiledKernel*));
    static_assert(sizeof(ResidencyHeat<ResidencyHeatTag_v::Warm, CompiledKernel*>)
                  == sizeof(CompiledKernel*));
    static_assert(sizeof(ResidencyHeat<ResidencyHeatTag_v::Cold, CompiledKernel*>)
                  == sizeof(CompiledKernel*));
}

// ── T12 — end-to-end Hot-fence consumer ─────────────────────────
//
// Production-like consumer: hot-dispatch path admits only L1 (Hot)
// kernels.  Models the per-op recording site that needs a
// ~5 ns kernel lookup; cold-cache penalties (~hundreds of ns) here
// would blow the per-call shape budget.
template <typename W>
    requires (W::template satisfies<ResidencyHeatTag_v::Hot>)
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

// ── T13 — phase-5-stub semantics ────────────────────────────────
static void test_phase5_stub_semantics() {
    KernelCache cache;
    FakeKernel fk{1};

    // Stubs are noexcept-callable.
    static_assert(noexcept(cache.lookup_l2(ContentHash{1}, RowHash{0})));
    static_assert(noexcept(cache.lookup_l3(ContentHash{1}, RowHash{0})));
    static_assert(noexcept(cache.publish_l2(ContentHash{1}, RowHash{0}, fk_ptr(&fk))));
    static_assert(noexcept(cache.publish_l3(ContentHash{1}, RowHash{0}, fk_ptr(&fk))));

    // l2/l3 lookups always miss today (no underlying store).
    auto l2_hit = cache.lookup_l2(ContentHash{0xEEEE}, RowHash{0});
    auto l3_hit = cache.lookup_l3(ContentHash{0xEEEE}, RowHash{0});
    assert(std::move(l2_hit).consume() == nullptr);
    assert(std::move(l3_hit).consume() == nullptr);

    // l2/l3 publishes succeed at the type level but DON'T persist —
    // a subsequent lookup_l2/l3 still returns nullptr.
    auto pub_l2 = cache.publish_l2(ContentHash{0xFFFF}, RowHash{0}, fk_ptr(&fk));
    auto pub_l3 = cache.publish_l3(ContentHash{0xFFFF}, RowHash{0}, fk_ptr(&fk));
    auto r2 = std::move(pub_l2).consume();
    auto r3 = std::move(pub_l3).consume();
    assert(r2.has_value());
    assert(r3.has_value());

    // Verify the stubs DON'T leak into the L1 path either — a
    // publish_l2 of a kernel under (FFFF, 0) is invisible to lookup_l1.
    auto check_l1 = cache.lookup_l1(ContentHash{0xFFFF}, RowHash{0});
    assert(std::move(check_l1).consume() == nullptr);
}

// ── T14 — three-level publish for the SAME (content, row) ───────
//
// Production scenario: a single kernel published to all three
// federation levels.  Each publication produces an independently-
// typed value; no cross-tier interchange at the type level.  Today
// only L1 has a real backing store; L2/L3 are Phase 5 stubs.
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
    assert(r2.has_value());
    assert(r3.has_value());

    // L1 lookup hits the just-published kernel.
    auto l1_hit = cache.lookup_l1(ContentHash{0x1111}, RowHash{0xAAAA});
    assert(std::move(l1_hit).consume() == fk_ptr(&fk));
}

// ── T15 — lookup_l1 miss path ──────────────────────────────────
//
// Audit-pass test: explicit MISS case via lookup_l1 (the main
// commit's tests only exercise hits).  An empty cache must return
// nullptr-pinned-Hot for every lookup, and the type-pin survives
// the miss.
static void test_lookup_l1_miss_returns_nullptr_pinned() {
    KernelCache cache;
    auto miss = cache.lookup_l1(ContentHash{0xDEAD}, RowHash{0xBEEF});
    static_assert(std::is_same_v<decltype(miss),
        ResidencyHeat<ResidencyHeatTag_v::Hot, CompiledKernel*>>);
    assert(std::move(miss).consume() == nullptr);

    // After publishing under a DIFFERENT key, the original key
    // still misses (no probe pollution).
    FakeKernel fk{1};
    auto pub = cache.publish_l1(ContentHash{0x1234}, RowHash{0}, fk_ptr(&fk));
    (void)std::move(pub).consume();

    auto miss2 = cache.lookup_l1(ContentHash{0xDEAD}, RowHash{0xBEEF});
    assert(std::move(miss2).consume() == nullptr);
}

// ── T16 — publish_l1 variant-update through the wrapper ────────
//
// Audit-pass test: publish_l1 followed by another publish_l1 under
// the SAME (content, row) pair must overwrite the slot — preserving
// the existing variant-update semantics of the underlying insert().
// Without this test, a refactor could accidentally make the wrapper
// reject re-publication, breaking optimizer-driven kernel-variant
// rotation (the production pattern where a better-compiled variant
// replaces an older one at the same key).
static void test_publish_l1_variant_update() {
    KernelCache cache;
    FakeKernel fk_v1{1};
    FakeKernel fk_v2{2};

    // Initial publish.
    auto p1 = cache.publish_l1(ContentHash{0xAAAA}, RowHash{0}, fk_ptr(&fk_v1));
    assert(std::move(p1).consume().has_value());

    // Variant-update: same key, new kernel pointer.
    auto p2 = cache.publish_l1(ContentHash{0xAAAA}, RowHash{0}, fk_ptr(&fk_v2));
    assert(std::move(p2).consume().has_value());

    // Lookup observes the newer variant.
    auto hit = cache.lookup_l1(ContentHash{0xAAAA}, RowHash{0});
    assert(std::move(hit).consume() == fk_ptr(&fk_v2));
}

// ── T17 — publish_l1 row-discrimination (FOUND-I05 inheritance) ─
//
// Audit-pass test: identical content_hash with distinct row_hash
// values must cache to DIFFERENT slots even when accessed via the
// L1 wrapper.  Pins the row-discrimination invariant (FOUND-I05)
// across the type-pinned overlay — without this test, a refactor
// that "simplifies" the wrapper to ignore row_hash would silently
// break the row-typed cache.
static void test_publish_l1_row_discrimination() {
    KernelCache cache;
    FakeKernel fk_row_a{1};
    FakeKernel fk_row_b{2};

    auto pa = cache.publish_l1(ContentHash{0x1234}, RowHash{0xAAAA}, fk_ptr(&fk_row_a));
    auto pb = cache.publish_l1(ContentHash{0x1234}, RowHash{0xBBBB}, fk_ptr(&fk_row_b));
    assert(std::move(pa).consume().has_value());
    assert(std::move(pb).consume().has_value());

    // Each row resolves to its own kernel.
    auto hit_a = cache.lookup_l1(ContentHash{0x1234}, RowHash{0xAAAA});
    auto hit_b = cache.lookup_l1(ContentHash{0x1234}, RowHash{0xBBBB});
    assert(std::move(hit_a).consume() == fk_ptr(&fk_row_a));
    assert(std::move(hit_b).consume() == fk_ptr(&fk_row_b));

    // A third row queries cleanly to nullptr (no cross-row pollution).
    auto miss = cache.lookup_l1(ContentHash{0x1234}, RowHash{0xCCCC});
    assert(std::move(miss).consume() == nullptr);
}

// ── T18 — cross-lattice non-mixing (ResidencyHeat ≠ CipherTier) ─
//
// Audit-pass test: ResidencyHeat<Hot, T> and CipherTier<Hot, T>
// have IDENTICAL tier-name spelling ("Hot") but are STRUCTURALLY
// distinct types — they sit on orthogonal lattices (cache-residency
// vs storage-residency).  Captures the bug class where a refactor
// folds "the three Hot wrappers" into a single shared template,
// silently allowing a Cipher RAM-replicated value to flow into a
// hot-dispatch path expecting an L1-resident kernel.
static void test_cross_lattice_non_mixing() {
    using crucible::safety::CipherTier;
    using crucible::safety::CipherTierTag_v;

    using RhHot = ResidencyHeat<ResidencyHeatTag_v::Hot, int>;
    using CtHot = CipherTier<CipherTierTag_v::Hot, int>;

    static_assert(!std::is_same_v<RhHot, CtHot>,
        "ResidencyHeat<Hot, T> and CipherTier<Hot, T> are orthogonal "
        "lattices — a Cipher-Hot value MUST NOT silently flow into "
        "a ResidencyHeat-Hot consumer or vice versa.  If this fires, "
        "a refactor has folded the wrappers into a single shared "
        "template, defeating the orthogonal-axis discipline.");

    // Cross-construction is rejected — neither wraps the other.
    static_assert(!std::is_constructible_v<RhHot, CtHot>);
    static_assert(!std::is_constructible_v<CtHot, RhHot>);
}

// ── T19 — Augur diagnostic tier-reader pattern ──────────────────
//
// Audit-pass test: the static `tier` accessor permits zero-runtime-
// cost dispatch on the cache-residency tier.  Augur's drift-
// attribution logic uses this to label residuals against the
// appropriate baseline ("L1 lookup miss rate" vs "L3 access
// latency" — distinct metrics that share the same RowHash but
// belong to different tier dashboards).  Today Augur isn't wired,
// but the static-tier API is the load-bearing primitive that
// Phase 3 Augur builds on; this test pins the API at the
// production call site, not just at the wrapper definition.
template <typename W>
[[nodiscard]] static constexpr int classify_cache_tier_for_augur() noexcept {
    if constexpr (W::tier == ResidencyHeatTag_v::Hot)  return 1;
    if constexpr (W::tier == ResidencyHeatTag_v::Warm) return 2;
    if constexpr (W::tier == ResidencyHeatTag_v::Cold) return 3;
    return 0;
}

static void test_augur_cache_tier_classifier() {
    KernelCache cache;
    FakeKernel fk{1};
    auto p = cache.publish_l1(ContentHash{0x9999}, RowHash{0}, fk_ptr(&fk));
    (void)std::move(p).consume();

    auto l1 = cache.lookup_l1(ContentHash{0x9999}, RowHash{0});
    auto l2 = cache.lookup_l2(ContentHash{0x9999}, RowHash{0});
    auto l3 = cache.lookup_l3(ContentHash{0x9999}, RowHash{0});

    static_assert(classify_cache_tier_for_augur<decltype(l1)>() == 1);
    static_assert(classify_cache_tier_for_augur<decltype(l2)>() == 2);
    static_assert(classify_cache_tier_for_augur<decltype(l3)>() == 3);

    (void)std::move(l1).consume();
    (void)std::move(l2).consume();
    (void)std::move(l3).consume();
}

// ── T20 — exhaustive cannot-tighten matrix ─────────────────────
//
// Audit-pass test: pins the SFINAE detection at the test site for
// every UPWARD relax<> attempt in the ResidencyHeat lattice.
// Mirror of the FOUND-G47-AUDIT T17 cannot-tighten test for
// CipherTier — the discipline is identical across all chain
// lattices, the test is per-lattice load-bearing.
template <typename W, ResidencyHeatTag_v T_target>
concept can_tighten = requires(W&& w) {
    { std::move(w).template relax<T_target>() };
};

static void test_cannot_tighten_to_stronger_tier() {
    using HotT  = ResidencyHeat<ResidencyHeatTag_v::Hot,  CompiledKernel*>;
    using WarmT = ResidencyHeat<ResidencyHeatTag_v::Warm, CompiledKernel*>;
    using ColdT = ResidencyHeat<ResidencyHeatTag_v::Cold, CompiledKernel*>;

    // Down (relax) — admissible.
    static_assert( can_tighten<HotT,  ResidencyHeatTag_v::Warm>);
    static_assert( can_tighten<HotT,  ResidencyHeatTag_v::Cold>);
    static_assert( can_tighten<WarmT, ResidencyHeatTag_v::Cold>);

    // Self — admissible (lattice reflexivity).
    static_assert( can_tighten<HotT,  ResidencyHeatTag_v::Hot>);
    static_assert( can_tighten<WarmT, ResidencyHeatTag_v::Warm>);
    static_assert( can_tighten<ColdT, ResidencyHeatTag_v::Cold>);

    // Up — REJECTED at every step (load-bearing).
    static_assert(!can_tighten<WarmT, ResidencyHeatTag_v::Hot>);
    static_assert(!can_tighten<ColdT, ResidencyHeatTag_v::Warm>);
    static_assert(!can_tighten<ColdT, ResidencyHeatTag_v::Hot>);
}

// ── T21 — FOUND-I06: L2 row_hash plumbing witness ──────────────
//
// FOUND-I06 — "L2 IR003* cache lookup updated to consume row_hash".
// The L1 cache (FOUND-I05) discriminates entries by (content_hash,
// row_hash); the L2 path is documented as the per-vendor-family
// cross-chip federation tier and its API surface accepts the same
// (ContentHash, RowHash) shape.  Today L2 is a Phase-5 stub
// (returns Warm-pinned nullptr regardless of inputs), but the
// row_hash slot MUST be plumbed end-to-end through the API so
// Phase 5 can wire the actual store without API churn.
//
// This test pins:
//   (a) lookup_l2 accepts diverse non-zero row_hashes — the API
//       does not silently truncate or special-case RowHash{0}.
//   (b) Each diverse-row_hash lookup returns Warm-pinned nullptr
//       (stub behavior — not influenced by row_hash value).
//   (c) publish_l2 accepts diverse row_hashes and returns the
//       success-marker for each (stub semantics).
//   (d) L2 stub with NON-empty L1 cache state does NOT accidentally
//       leak into or mirror L1 — L2 lookups stay nullptr even when
//       L1 has a live entry at the same (content, row).
static void test_l2_row_hash_plumbing_FOUND_I06() {
    KernelCache cache;
    FakeKernel fk{1};

    // (a) + (b) — diverse row_hashes, each lookup_l2 returns nullptr-Warm.
    constexpr RowHash diverse_rows[] = {
        RowHash{0x0001}, RowHash{0x00FF}, RowHash{0xAAAA},
        RowHash{0xDEADBEEF}, RowHash{0xFFFFFFFFFFFFFFFFULL},
    };
    for (auto rh : diverse_rows) {
        auto pinned = cache.lookup_l2(ContentHash{0x10000}, rh);
        // Type identity preserved across all row_hashes.
        static_assert(std::is_same_v<decltype(pinned),
            ResidencyHeat<ResidencyHeatTag_v::Warm, CompiledKernel*>>);
        assert(std::move(pinned).consume() == nullptr);
    }

    // (c) — publish_l2 with diverse row_hashes succeeds at type level.
    for (auto rh : diverse_rows) {
        auto pub = cache.publish_l2(ContentHash{0x20000}, rh, fk_ptr(&fk));
        auto r = std::move(pub).consume();
        assert(r.has_value());
    }

    // (d) — populate L1 at (0x30000, 0xCAFE), then verify L2 stays
    // nullptr for the SAME key.  Pins that L2 stub does not mirror
    // L1's live entries — when Phase 5 wires the L2 store, this
    // test continues to red ONLY if the implementation correctly
    // treats L2 as a separate physical store from L1 (not a fallback
    // to L1).  The test will be UPDATED at that time with explicit
    // l2-publish-then-l2-lookup expectations.
    auto pub_l1 = cache.publish_l1(
        ContentHash{0x30000}, RowHash{0xCAFE}, fk_ptr(&fk));
    (void)std::move(pub_l1).consume();
    auto l1_hit = cache.lookup_l1(ContentHash{0x30000}, RowHash{0xCAFE});
    assert(std::move(l1_hit).consume() == fk_ptr(&fk));  // L1 has it.
    auto l2_miss = cache.lookup_l2(ContentHash{0x30000}, RowHash{0xCAFE});
    assert(std::move(l2_miss).consume() == nullptr);     // L2 doesn't.
}

// ── T22 — FOUND-I07: L3 row_hash plumbing witness ──────────────
//
// FOUND-I07 — "L3 compiled-bytes cache lookup updated to consume
// row_hash".  Mirror of T21 for the L3 (per-chip cold archive)
// tier.  Same Phase-5-stub semantics as L2; same API discipline.
// Pins the row_hash plumbing through lookup_l3 / publish_l3 so
// Phase 5's S3-backed L3 store can drop in without API churn.
static void test_l3_row_hash_plumbing_FOUND_I07() {
    KernelCache cache;
    FakeKernel fk{1};

    constexpr RowHash diverse_rows[] = {
        RowHash{0x0001}, RowHash{0x00FF}, RowHash{0xAAAA},
        RowHash{0xDEADBEEF}, RowHash{0xFFFFFFFFFFFFFFFFULL},
    };

    // (a) + (b) — diverse row_hashes, each lookup_l3 returns nullptr-Cold.
    for (auto rh : diverse_rows) {
        auto pinned = cache.lookup_l3(ContentHash{0x40000}, rh);
        static_assert(std::is_same_v<decltype(pinned),
            ResidencyHeat<ResidencyHeatTag_v::Cold, CompiledKernel*>>);
        assert(std::move(pinned).consume() == nullptr);
    }

    // (c) — publish_l3 with diverse row_hashes succeeds at type level.
    for (auto rh : diverse_rows) {
        auto pub = cache.publish_l3(ContentHash{0x50000}, rh, fk_ptr(&fk));
        auto r = std::move(pub).consume();
        assert(r.has_value());
    }

    // (d) — populate L1 at (0x60000, 0xBABE), then verify L3 stays
    // nullptr for the SAME key.  Same isolation invariant as T21
    // for L2.
    auto pub_l1 = cache.publish_l1(
        ContentHash{0x60000}, RowHash{0xBABE}, fk_ptr(&fk));
    (void)std::move(pub_l1).consume();
    auto l1_hit = cache.lookup_l1(ContentHash{0x60000}, RowHash{0xBABE});
    assert(std::move(l1_hit).consume() == fk_ptr(&fk));
    auto l3_miss = cache.lookup_l3(ContentHash{0x60000}, RowHash{0xBABE});
    assert(std::move(l3_miss).consume() == nullptr);
}

// ── T23 — FOUND-I06/I07-AUDIT: L2 ↔ L3 cross-tier isolation ────
//
// FOUND-I06/I07-AUDIT (Finding A) — T13 covers L1↔L2 and L1↔L3
// isolation (publish_l2/l3 don't leak into L1).  But the
// L2↔L3 pair was never directly witnessed.  When Phase 5 wires
// the actual L2 (per-vendor-family) and L3 (per-chip cold
// archive) stores, a refactor that accidentally shared the
// backing store between the two tiers (e.g., a single shared
// hash table mistakenly indexed by both lookup_l2 and lookup_l3)
// would not be caught by any existing test.  T23 closes the
// final isolation cell: publish_l2 at a key MUST be invisible
// to lookup_l3 at the same key, and vice-versa.
//
// Today the assertion holds trivially because both stubs return
// nullptr regardless of inputs.  When Phase 5 lands real backing
// stores, T23 will be the load-bearing witness that those stores
// are physically separate.
static void test_l2_l3_cross_tier_isolation_FOUND_I06_I07_AUDIT() {
    KernelCache cache;
    FakeKernel fk_l2_only{42};
    FakeKernel fk_l3_only{43};

    // Publish ONLY into L2 at (0x70000, 0xD00D).
    auto p2 = cache.publish_l2(
        ContentHash{0x70000}, RowHash{0xD00D}, fk_ptr(&fk_l2_only));
    (void)std::move(p2).consume();

    // L3 at the same key MUST miss — L2 publish does not leak into L3.
    auto l3_miss = cache.lookup_l3(ContentHash{0x70000}, RowHash{0xD00D});
    assert(std::move(l3_miss).consume() == nullptr);

    // Now publish ONLY into L3 at a different key (0x80000, 0xF00D).
    auto p3 = cache.publish_l3(
        ContentHash{0x80000}, RowHash{0xF00D}, fk_ptr(&fk_l3_only));
    (void)std::move(p3).consume();

    // L2 at the same key MUST miss — L3 publish does not leak into L2.
    auto l2_miss = cache.lookup_l2(ContentHash{0x80000}, RowHash{0xF00D});
    assert(std::move(l2_miss).consume() == nullptr);

    // Cross-publication at a SHARED key (0x90000, 0xCEED) — both L2
    // and L3 should hold their own state independently.
    auto p2_shared = cache.publish_l2(
        ContentHash{0x90000}, RowHash{0xCEED}, fk_ptr(&fk_l2_only));
    auto p3_shared = cache.publish_l3(
        ContentHash{0x90000}, RowHash{0xCEED}, fk_ptr(&fk_l3_only));
    (void)std::move(p2_shared).consume();
    (void)std::move(p3_shared).consume();

    // Both lookups today return nullptr (Phase-5 stub).  When Phase 5
    // wires real stores, EACH side should observe its OWN published
    // kernel (not the other side's).  T23 will be tightened at that
    // time to assert per-tier kernel identity.
    auto l2_shared = cache.lookup_l2(ContentHash{0x90000}, RowHash{0xCEED});
    auto l3_shared = cache.lookup_l3(ContentHash{0x90000}, RowHash{0xCEED});
    assert(std::move(l2_shared).consume() == nullptr);
    assert(std::move(l3_shared).consume() == nullptr);
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
    test_augur_cache_tier_classifier();
    test_cannot_tighten_to_stronger_tier();
    test_l2_row_hash_plumbing_FOUND_I06();
    test_l3_row_hash_plumbing_FOUND_I07();
    test_l2_l3_cross_tier_isolation_FOUND_I06_I07_AUDIT();

    std::puts("ok");
    return 0;
}
