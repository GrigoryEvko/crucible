// FOUND-G47 — Cipher CipherTier-pinned publication surface.
//
// Verifies the publish_hot / publish_warm / publish_cold variants
// added to Cipher.  These are the production call sites for the
// CipherTier wrapper (FOUND-G44 substrate, FOUND-G46 negative-compile
// fixtures).
//
//   publish_warm  → Wraps store() — REAL NVMe write.
//                   Returns cipher_tier::Warm<ContentHash>.
//   publish_hot   → Phase 5 stub.  Returns
//                   cipher_tier::Hot<ContentHash>{ContentHash{}}.
//   publish_cold  → Phase 5 stub.  Returns
//                   cipher_tier::Cold<ContentHash>{ContentHash{}}.
//
// Test surface coverage (T01-T13):
//   T01 — publish_warm bit-equality vs raw store
//   T02 — publish_warm type-identity
//   T03 — publish_hot type-identity (stub returns none-hash)
//   T04 — publish_cold type-identity (stub returns none-hash)
//   T05 — typed-view variant + legacy variant return same type
//   T06 — fence-acceptance simulation: Hot subsumes Warm/Cold
//   T07 — fence-acceptance simulation: Warm rejected at Hot fence
//   T08 — fence-acceptance simulation: Cold rejected at Warm/Hot fences
//   T09 — relax DOWN-the-lattice (Hot → Warm → Cold)
//   T10 — layout invariant (sizeof preservation)
//   T11 — end-to-end Hot-fence consumer (Keeper hot-reshard simulation)
//   T12 — end-to-end Warm-fence consumer (publish_hot relaxed admits)
//   T13 — phase-5-stub semantics: Hot/Cold return none-hash, callable

#include <crucible/Cipher.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/safety/CipherTier.h>
#include "test_assert.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <type_traits>
#include <utility>

using crucible::Cipher;
using crucible::ContentHash;
using crucible::RegionNode;
using crucible::Arena;
using crucible::TraceEntry;
using crucible::SchemaHash;
using crucible::OpIndex;
using crucible::SlotId;
using crucible::TensorMeta;
using crucible::ScalarType;
using crucible::safety::CipherTier;
using crucible::safety::CipherTierTag_v;

static crucible::effects::Test g_test;

// Build a minimal RegionNode (mirrors test_cipher_wait.cpp's helper).
static RegionNode* make_test_region(Arena& arena, uint32_t seed) {
    constexpr uint32_t NUM_OPS = 1;
    auto* ops = arena.alloc_array<TraceEntry>(g_test.alloc, NUM_OPS);
    std::uninitialized_value_construct_n(ops, NUM_OPS);
    ops[0].schema_hash = SchemaHash{0xCAFEBABE00000000ULL + seed};
    ops[0].num_inputs  = 1;
    ops[0].num_outputs = 1;
    ops[0].input_metas = arena.alloc_array<TensorMeta>(g_test.alloc, 1);
    ops[0].input_metas[0] = {};
    ops[0].input_metas[0].ndim = 1;
    ops[0].input_metas[0].sizes[0] = 16;
    ops[0].input_metas[0].strides[0] = 1;
    ops[0].input_metas[0].dtype = ScalarType::Float;
    ops[0].output_metas = arena.alloc_array<TensorMeta>(g_test.alloc, 1);
    ops[0].output_metas[0] = ops[0].input_metas[0];
    ops[0].input_trace_indices = arena.alloc_array<OpIndex>(g_test.alloc, 1);
    ops[0].input_trace_indices[0] = OpIndex{};
    ops[0].input_slot_ids = arena.alloc_array<SlotId>(g_test.alloc, 1);
    ops[0].input_slot_ids[0] = SlotId{};
    ops[0].output_slot_ids = arena.alloc_array<SlotId>(g_test.alloc, 1);
    ops[0].output_slot_ids[0] = SlotId{seed};
    return crucible::make_region(g_test.alloc, arena, ops, NUM_OPS);
}

// ── T01 — publish_warm bit-equality vs raw store ────────────────
static void test_publish_warm_bit_equality(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 1);
    auto cipher = Cipher::open(dir);

    // Raw store + publish_warm on the same region must yield the
    // same ContentHash (idempotent — second call hits the
    // already-exists fast path).
    ContentHash raw = cipher.store(region, nullptr);
    auto warm = cipher.publish_warm(region, nullptr);
    ContentHash via_wrapper = std::move(warm).consume();
    assert(raw == via_wrapper);
    assert(static_cast<bool>(raw));
}

// ── T02 — publish_warm type-identity ────────────────────────────
static void test_publish_warm_type_identity(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 2);
    auto cipher = Cipher::open(dir);

    using Got  = decltype(cipher.publish_warm(region, nullptr));
    using Want = CipherTier<CipherTierTag_v::Warm, ContentHash>;
    static_assert(std::is_same_v<Got, Want>,
        "publish_warm must return CipherTier<Warm, ContentHash>");
    static_assert(Got::tier == CipherTierTag_v::Warm);

    // Consume so the value isn't discarded.
    auto p = cipher.publish_warm(region, nullptr);
    (void)std::move(p).consume();
}

// ── T03 — publish_hot type-identity (Phase 5 stub) ──────────────
static void test_publish_hot_type_identity(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 3);
    auto cipher = Cipher::open(dir);

    using Got  = decltype(cipher.publish_hot(region, nullptr));
    using Want = CipherTier<CipherTierTag_v::Hot, ContentHash>;
    static_assert(std::is_same_v<Got, Want>,
        "publish_hot must return CipherTier<Hot, ContentHash>");
    static_assert(Got::tier == CipherTierTag_v::Hot);

    // Phase 5 stub: returns none-hash today.
    auto p = cipher.publish_hot(region, nullptr);
    ContentHash h = std::move(p).consume();
    assert(!static_cast<bool>(h));   // Phase 5 stub semantics
}

// ── T04 — publish_cold type-identity (Phase 5 stub) ─────────────
static void test_publish_cold_type_identity(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 4);
    auto cipher = Cipher::open(dir);

    using Got  = decltype(cipher.publish_cold(region, nullptr));
    using Want = CipherTier<CipherTierTag_v::Cold, ContentHash>;
    static_assert(std::is_same_v<Got, Want>,
        "publish_cold must return CipherTier<Cold, ContentHash>");
    static_assert(Got::tier == CipherTierTag_v::Cold);

    // Phase 5 stub: returns none-hash today.
    auto p = cipher.publish_cold(region, nullptr);
    ContentHash h = std::move(p).consume();
    assert(!static_cast<bool>(h));   // Phase 5 stub semantics
}

// ── T05 — typed-view variant + legacy variant return same type ──
static void test_view_and_legacy_overload_parity(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 5);
    auto cipher = Cipher::open(dir);

    auto view = cipher.mint_open_view();

    // publish_warm: both shapes return Warm-pinned.
    using WarmGot1 = decltype(cipher.publish_warm(view, region, nullptr));
    using WarmGot2 = decltype(cipher.publish_warm(region, nullptr));
    static_assert(std::is_same_v<WarmGot1, WarmGot2>);

    // publish_hot: both shapes return Hot-pinned.
    using HotGot1 = decltype(cipher.publish_hot(view, region, nullptr));
    using HotGot2 = decltype(cipher.publish_hot(region, nullptr));
    static_assert(std::is_same_v<HotGot1, HotGot2>);

    // publish_cold: both shapes return Cold-pinned.
    using ColdGot1 = decltype(cipher.publish_cold(view, region, nullptr));
    using ColdGot2 = decltype(cipher.publish_cold(region, nullptr));
    static_assert(std::is_same_v<ColdGot1, ColdGot2>);

    // Consume each so values aren't dropped on the floor.
    (void)std::move(cipher.publish_warm(view, region, nullptr)).consume();
    (void)std::move(cipher.publish_hot(view,  region, nullptr)).consume();
    (void)std::move(cipher.publish_cold(view, region, nullptr)).consume();
}

// ── T06 — fence-acceptance: Hot subsumes Warm and Cold ──────────
static void test_hot_satisfies_weaker_tiers() {
    using Hot  = CipherTier<CipherTierTag_v::Hot,  ContentHash>;

    // Hot satisfies every tier (top of lattice).
    static_assert( Hot::satisfies<CipherTierTag_v::Hot>);
    static_assert( Hot::satisfies<CipherTierTag_v::Warm>);
    static_assert( Hot::satisfies<CipherTierTag_v::Cold>);
}

// ── T07 — fence-acceptance: Warm rejected at Hot fence ──────────
static void test_warm_rejected_at_hot_fence() {
    using Warm = CipherTier<CipherTierTag_v::Warm, ContentHash>;

    // Warm satisfies Warm + Cold but FAILS Hot.
    static_assert( Warm::satisfies<CipherTierTag_v::Warm>);
    static_assert( Warm::satisfies<CipherTierTag_v::Cold>);
    static_assert(!Warm::satisfies<CipherTierTag_v::Hot>,
        "Warm MUST NOT satisfy Hot — load-bearing rejection for the "
        "Keeper hot-tier reincarnation gate.");
}

// ── T08 — fence-acceptance: Cold rejected at Warm/Hot fences ────
static void test_cold_rejected_at_higher_fences() {
    using Cold = CipherTier<CipherTierTag_v::Cold, ContentHash>;

    static_assert( Cold::satisfies<CipherTierTag_v::Cold>);
    static_assert(!Cold::satisfies<CipherTierTag_v::Warm>);
    static_assert(!Cold::satisfies<CipherTierTag_v::Hot>);
}

// ── T09 — relax DOWN-the-lattice (Hot → Warm → Cold) ────────────
static void test_relax_to_weaker_tiers(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 9);
    auto cipher = Cipher::open(dir);

    // Hot → Warm → Cold chain.  Each relax preserves the value bytes.
    auto hot = cipher.publish_hot(region, nullptr);
    auto warm = std::move(hot).relax<CipherTierTag_v::Warm>();
    static_assert(std::is_same_v<decltype(warm),
        CipherTier<CipherTierTag_v::Warm, ContentHash>>);

    auto cold = std::move(warm).relax<CipherTierTag_v::Cold>();
    static_assert(std::is_same_v<decltype(cold),
        CipherTier<CipherTierTag_v::Cold, ContentHash>>);

    ContentHash h = std::move(cold).consume();
    (void)h;
}

// ── T10 — layout invariant ──────────────────────────────────────
static void test_layout_invariant() {
    static_assert(sizeof(CipherTier<CipherTierTag_v::Hot,  ContentHash>)
                  == sizeof(ContentHash));
    static_assert(sizeof(CipherTier<CipherTierTag_v::Warm, ContentHash>)
                  == sizeof(ContentHash));
    static_assert(sizeof(CipherTier<CipherTierTag_v::Cold, ContentHash>)
                  == sizeof(ContentHash));
}

// ── T11 — end-to-end Hot-fence consumer ─────────────────────────
//
// Simulates Keeper's hot-tier reincarnation admission gate.  The
// consumer demands CipherTier<Hot> at the type level; only
// publish_hot's return value passes.
template <typename W>
    requires (W::template satisfies<CipherTierTag_v::Hot>)
static ContentHash hot_reshard_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

static void test_e2e_hot_fence_consumer(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 11);
    auto cipher = Cipher::open(dir);

    auto pinned = cipher.publish_hot(region, nullptr);
    ContentHash h = hot_reshard_consumer(std::move(pinned));
    // Phase 5 stub: returns none-hash; type passed the gate.
    assert(!static_cast<bool>(h));
}

// ── T12 — end-to-end Warm-fence consumer accepts relaxed Hot ────
template <typename W>
    requires (W::template satisfies<CipherTierTag_v::Warm>)
static ContentHash warm_publish_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

static void test_e2e_warm_fence_admits_hot_and_warm(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 12);
    auto cipher = Cipher::open(dir);

    // publish_warm value passes the Warm gate (self-admission).
    auto warm_val = cipher.publish_warm(region, nullptr);
    ContentHash h_warm = warm_publish_consumer(std::move(warm_val));
    assert(static_cast<bool>(h_warm));   // real NVMe write happened

    // publish_hot value also passes the Warm gate (subsumption).
    auto hot_val = cipher.publish_hot(region, nullptr);
    ContentHash h_hot = warm_publish_consumer(std::move(hot_val));
    (void)h_hot;   // Phase 5 stub returns none-hash; type passed
}

// ── T13 — phase-5-stub semantics: Hot/Cold callable + none-hash ─
static void test_phase5_stub_semantics(const char* dir) {
    Arena arena(1 << 16);
    auto* region = make_test_region(arena, 13);
    auto cipher = Cipher::open(dir);

    // The stubs are noexcept-callable — Phase 5 doesn't touch the
    // hot path at all.
    static_assert(noexcept(cipher.publish_hot(region, nullptr)));
    static_assert(noexcept(cipher.publish_cold(region, nullptr)));

    // Both stubs return none-hash today.  When Phase 5 ships real
    // backends, this test must be updated — the stub-detection
    // contract is then no longer testable from here.
    auto hot  = cipher.publish_hot(region,  nullptr);
    auto cold = cipher.publish_cold(region, nullptr);
    ContentHash h_hot  = std::move(hot).consume();
    ContentHash h_cold = std::move(cold).consume();
    assert(!static_cast<bool>(h_hot));
    assert(!static_cast<bool>(h_cold));
}

int main() {
    char tmpdir[] = "/tmp/crucible_cipher_publish_XXXXXX";
    char* dir = mkdtemp(tmpdir);
    assert(dir != nullptr);

    test_publish_warm_bit_equality(dir);
    test_publish_warm_type_identity(dir);
    test_publish_hot_type_identity(dir);
    test_publish_cold_type_identity(dir);
    test_view_and_legacy_overload_parity(dir);
    test_hot_satisfies_weaker_tiers();
    test_warm_rejected_at_hot_fence();
    test_cold_rejected_at_higher_fences();
    test_relax_to_weaker_tiers(dir);
    test_layout_invariant();
    test_e2e_hot_fence_consumer(dir);
    test_e2e_warm_fence_admits_hot_and_warm(dir);
    test_phase5_stub_semantics(dir);

    std::filesystem::remove_all(dir);
    std::puts("ok");
    return 0;
}
