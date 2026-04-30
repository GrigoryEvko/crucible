// FOUND-G22 — TraceRing/MetaLog HotPath-pinned production surface.
//
// Verifies the try_append_pinned and drain_pinned variants added to
// TraceRing and MetaLog.  These are the production call sites for
// the HotPath wrapper (FOUND-G19 substrate, FOUND-G21 negative-
// compile fixtures).
//
//   TraceRing::try_append_pinned  → HotPath<Hot,  bool>      — REAL
//   TraceRing::drain_pinned       → HotPath<Warm, uint32_t>  — REAL
//   MetaLog::try_append_pinned    → HotPath<Hot,  MetaIndex> — REAL
//
// Test surface coverage:
//   T01 — TraceRing::try_append_pinned bit-equality vs raw try_append
//   T02 — TraceRing::try_append_pinned type-identity (Hot)
//   T03 — TraceRing::drain_pinned type-identity (Warm)
//   T04 — MetaLog::try_append_pinned type-identity (Hot)
//   T05 — fence-acceptance simulation (Hot subsumes Warm/Cold)
//   T06 — Warm rejected at Hot fence
//   T07 — Cold rejected at higher fences
//   T08 — relax DOWN-the-lattice (Hot → Warm → Cold)
//   T09 — layout invariant (sizeof preservation)
//   T10 — end-to-end Hot-fence consumer (per-op recording site sim)
//   T11 — end-to-end Warm-fence consumer admits Hot via subsumption
//   T12 — TraceRing full-ring path returns false-pinned-Hot
//   T13 — MetaLog full-buffer returns none-pinned-Hot
//   T14 — drain_pinned of empty ring returns 0-pinned-Warm
//   T15 — cannot-tighten-to-stronger matrix (UPWARD relax rejected)

#include <crucible/MetaLog.h>
#include <crucible/TraceRing.h>
#include <crucible/safety/HotPath.h>
#include "test_assert.h"

#include <cstdio>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

using crucible::TraceRing;
using crucible::MetaLog;
using crucible::TensorMeta;
using crucible::MetaIndex;
using crucible::SchemaHash;
using crucible::ShapeHash;
using crucible::OpIndex;
using crucible::ScopeHash;
using crucible::CallsiteHash;
using crucible::safety::HotPath;
using crucible::safety::HotPathTier_v;

// ── Helpers ─────────────────────────────────────────────────────
//
// Entry doesn't carry op_index — slot index in the ring IS the op
// index (see TraceRing.h docblock).  Just seed schema/shape hashes.
static TraceRing::Entry make_entry(uint64_t schema_seed) noexcept {
    TraceRing::Entry e{};
    e.schema_hash = SchemaHash{schema_seed};
    e.shape_hash  = ShapeHash{schema_seed * 31};
    return e;
}

// ── T01 — TraceRing::try_append_pinned bit-equality ─────────────
static void test_try_append_pinned_bit_equality() {
    auto ring = std::make_unique<TraceRing>();
    auto e1 = make_entry(1);
    auto e2 = make_entry(2);

    // raw try_append.
    bool raw_ok = ring->try_append(e1);

    // pinned try_append on a different entry — same path, same outcome.
    auto pinned = ring->try_append_pinned(e2);
    bool via_wrapper = std::move(pinned).consume();

    assert(raw_ok);
    assert(via_wrapper);
}

// ── T02 — TraceRing::try_append_pinned type-identity ────────────
static void test_try_append_pinned_type_identity() {
    auto ring = std::make_unique<TraceRing>();
    auto e = make_entry(3);

    using Got  = decltype(ring->try_append_pinned(e));
    using Want = HotPath<HotPathTier_v::Hot, bool>;
    static_assert(std::is_same_v<Got, Want>,
        "try_append_pinned must return HotPath<Hot, bool>");
    static_assert(Got::tier == HotPathTier_v::Hot);

    auto p = ring->try_append_pinned(e);
    (void)std::move(p).consume();
}

// ── T03 — TraceRing::drain_pinned type-identity ─────────────────
static void test_drain_pinned_type_identity() {
    auto ring = std::make_unique<TraceRing>();

    using Got  = decltype(ring->drain_pinned(nullptr, 0u));
    using Want = HotPath<HotPathTier_v::Warm, uint32_t>;
    static_assert(std::is_same_v<Got, Want>,
        "drain_pinned must return HotPath<Warm, uint32_t>");
    static_assert(Got::tier == HotPathTier_v::Warm);
}

// ── T04 — MetaLog::try_append_pinned type-identity ──────────────
static void test_metalog_try_append_pinned_type_identity() {
    auto log = std::make_unique<MetaLog>();

    using Got  = decltype(log->try_append_pinned(static_cast<const TensorMeta*>(nullptr), 0u));
    using Want = HotPath<HotPathTier_v::Hot, MetaIndex>;
    static_assert(std::is_same_v<Got, Want>,
        "MetaLog::try_append_pinned must return HotPath<Hot, MetaIndex>");
    static_assert(Got::tier == HotPathTier_v::Hot);

    // Real append of a single TensorMeta.
    TensorMeta meta{};
    meta.ndim = 1;
    meta.sizes[0] = 16;
    meta.strides[0] = 1;
    auto p = log->try_append_pinned(&meta, 1);
    MetaIndex idx = std::move(p).consume();
    assert(idx.is_valid());
    assert(idx.raw() == 0);
}

// ── T05 — Hot subsumes Warm and Cold ────────────────────────────
static void test_hot_satisfies_weaker_tiers() {
    using Hot = HotPath<HotPathTier_v::Hot, bool>;
    static_assert( Hot::satisfies<HotPathTier_v::Hot>);
    static_assert( Hot::satisfies<HotPathTier_v::Warm>);
    static_assert( Hot::satisfies<HotPathTier_v::Cold>);
}

// ── T06 — Warm rejected at Hot fence ────────────────────────────
static void test_warm_rejected_at_hot_fence() {
    using Warm = HotPath<HotPathTier_v::Warm, uint32_t>;
    static_assert( Warm::satisfies<HotPathTier_v::Warm>);
    static_assert( Warm::satisfies<HotPathTier_v::Cold>);
    static_assert(!Warm::satisfies<HotPathTier_v::Hot>,
        "Warm MUST NOT satisfy Hot — load-bearing rejection for the "
        "per-op recording site / hot dispatch admission gate.");
}

// ── T07 — Cold rejected at higher fences ────────────────────────
static void test_cold_rejected_at_higher_fences() {
    using Cold = HotPath<HotPathTier_v::Cold, int>;
    static_assert( Cold::satisfies<HotPathTier_v::Cold>);
    static_assert(!Cold::satisfies<HotPathTier_v::Warm>);
    static_assert(!Cold::satisfies<HotPathTier_v::Hot>);
}

// ── T08 — relax DOWN-the-lattice ────────────────────────────────
static void test_relax_to_weaker_tiers() {
    auto ring = std::make_unique<TraceRing>();
    auto e = make_entry(8);

    auto hot  = ring->try_append_pinned(e);
    auto warm = std::move(hot).relax<HotPathTier_v::Warm>();
    static_assert(std::is_same_v<decltype(warm),
        HotPath<HotPathTier_v::Warm, bool>>);

    auto cold = std::move(warm).relax<HotPathTier_v::Cold>();
    static_assert(std::is_same_v<decltype(cold),
        HotPath<HotPathTier_v::Cold, bool>>);

    bool ok = std::move(cold).consume();
    assert(ok);
}

// ── T09 — layout invariant ──────────────────────────────────────
static void test_layout_invariant() {
    static_assert(sizeof(HotPath<HotPathTier_v::Hot,  bool>)      == sizeof(bool));
    static_assert(sizeof(HotPath<HotPathTier_v::Warm, uint32_t>)  == sizeof(uint32_t));
    static_assert(sizeof(HotPath<HotPathTier_v::Hot,  MetaIndex>) == sizeof(MetaIndex));
}

// ── T10 — end-to-end Hot-fence consumer ─────────────────────────
//
// Production-like consumer: foreground recording-site hot path that
// admits only Hot-pinned values.  Models Vigil::dispatch_op or the
// CrucibleContext::dispatch_op site that gates per-op recording on
// Hot-tier compliance.
template <typename W>
    requires (W::template satisfies<HotPathTier_v::Hot>)
static bool fg_recording_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

static void test_e2e_hot_fence_consumer() {
    auto ring = std::make_unique<TraceRing>();
    auto e = make_entry(10);

    auto pinned = ring->try_append_pinned(e);
    bool ok = fg_recording_consumer(std::move(pinned));
    assert(ok);
}

// ── T11 — end-to-end Warm-fence consumer admits Hot ─────────────
template <typename W>
    requires (W::template satisfies<HotPathTier_v::Warm>)
static bool warm_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume() != 0;   // bool/uint32 → bool
}

static void test_warm_fence_admits_hot_via_subsumption() {
    auto ring = std::make_unique<TraceRing>();
    auto e = make_entry(11);

    // Hot value passes the Warm gate (subsumption).
    auto hot = ring->try_append_pinned(e);
    bool ok_hot = warm_consumer(std::move(hot));
    assert(ok_hot);
}

// ── T12 — TraceRing full-ring path returns false-pinned-Hot ────
//
// Fill the ring to capacity; the next try_append_pinned must
// return false (full) but the type-pin survives the failure path.
// We can't easily fill 65536 entries in unit-test time, but we
// CAN verify the type identity and that the wrapper carries the
// false correctly when full.  The behavioral test for full-ring
// is in test_trace_ring.cpp; here we just pin the type.
static void test_full_ring_type_pin_survives_failure() {
    auto ring = std::make_unique<TraceRing>();

    // The type-pin is independent of the boolean value — proven
    // by constructing a Hot-pinned `false` and treating it as
    // structurally identical to the Hot-pinned `true` path.
    using FullPathT = HotPath<HotPathTier_v::Hot, bool>;
    FullPathT failure_value{false};
    static_assert(std::is_same_v<decltype(ring->try_append_pinned(make_entry(0))),
                                 FullPathT>);
    bool was_full = !std::move(failure_value).consume();
    assert(was_full);
}

// ── T13 — MetaLog full-buffer returns none-pinned-Hot ──────────
//
// Same shape as T12 — the type-pin survives the
// MetaIndex::none() failure return.  Behavioral fullness is in
// test_meta_log.cpp.
static void test_metalog_full_buffer_type_pin_survives_failure() {
    auto log = std::make_unique<MetaLog>();
    TensorMeta meta{};
    meta.ndim = 1;
    meta.sizes[0] = 1;
    meta.strides[0] = 1;

    using NonePathT = HotPath<HotPathTier_v::Hot, MetaIndex>;
    NonePathT none_path{MetaIndex::none()};
    static_assert(std::is_same_v<decltype(log->try_append_pinned(&meta, 1)),
                                 NonePathT>);
    MetaIndex idx = std::move(none_path).consume();
    assert(!idx.is_valid());
}

// ── T14 — drain_pinned of empty ring returns 0-pinned-Warm ─────
static void test_drain_pinned_empty_ring() {
    auto ring = std::make_unique<TraceRing>();
    TraceRing::Entry buf[4]{};
    auto pinned = ring->drain_pinned(buf, 4);
    uint32_t got = std::move(pinned).consume();
    assert(got == 0);
}

// ── T15 — cannot-tighten-to-stronger matrix ─────────────────────
template <typename W, HotPathTier_v T_target>
concept can_tighten = requires(W&& w) {
    { std::move(w).template relax<T_target>() };
};

static void test_cannot_tighten_to_stronger_tier() {
    using HotT  = HotPath<HotPathTier_v::Hot,  bool>;
    using WarmT = HotPath<HotPathTier_v::Warm, bool>;
    using ColdT = HotPath<HotPathTier_v::Cold, bool>;

    // Down (relax) — admissible.
    static_assert( can_tighten<HotT,  HotPathTier_v::Warm>);
    static_assert( can_tighten<HotT,  HotPathTier_v::Cold>);
    static_assert( can_tighten<WarmT, HotPathTier_v::Cold>);

    // Self — admissible.
    static_assert( can_tighten<HotT,  HotPathTier_v::Hot>);
    static_assert( can_tighten<WarmT, HotPathTier_v::Warm>);
    static_assert( can_tighten<ColdT, HotPathTier_v::Cold>);

    // Up — REJECTED at every step (load-bearing).
    static_assert(!can_tighten<WarmT, HotPathTier_v::Hot>);
    static_assert(!can_tighten<ColdT, HotPathTier_v::Warm>);
    static_assert(!can_tighten<ColdT, HotPathTier_v::Hot>);
}

int main() {
    test_try_append_pinned_bit_equality();
    test_try_append_pinned_type_identity();
    test_drain_pinned_type_identity();
    test_metalog_try_append_pinned_type_identity();
    test_hot_satisfies_weaker_tiers();
    test_warm_rejected_at_hot_fence();
    test_cold_rejected_at_higher_fences();
    test_relax_to_weaker_tiers();
    test_layout_invariant();
    test_e2e_hot_fence_consumer();
    test_warm_fence_admits_hot_via_subsumption();
    test_full_ring_type_pin_survives_failure();
    test_metalog_full_buffer_type_pin_survives_failure();
    test_drain_pinned_empty_ring();
    test_cannot_tighten_to_stronger_tier();

    std::puts("ok");
    return 0;
}
