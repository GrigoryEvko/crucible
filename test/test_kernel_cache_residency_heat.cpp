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

    std::puts("ok");
    return 0;
}
