#pragma once

// ═══════════════════════════════════════════════════════════════════
// fixy/AsyncPipeline.h — production facade: DERIVE the async-copy
//                        pipeline parameters from the static MemoryPlan,
//                        then mint the V-273 AsyncPipeline session.
//                        (FIXY-V-274)
//
// V-273 ships the protocol (sessions/AsyncPipelineSession.h) parameterised
// on Bytes / Stages / Scope.  Those were free knobs the caller had to
// guess.  This facade makes them STRUCTURAL: it reads the liveness-analysed
// MemoryPlan and binds the only correct values —
//
//   * Bytes  (mbarrier expect_tx) = the planned slot's `TensorSlot::nbytes`
//             (MerkleDag.h).  Bound through `Refined<equals_slot_size<N>>`
//             (ExpectTxBytes<N>): a producer arrive.expect_tx that
//             disagrees with the planned slot size is a CONTRACT VIOLATION
//             at session establishment, not a consumer that hangs in
//             try_wait forever.  When the plan is a compile constant the
//             mismatch is a COMPILE error (consteval CRUCIBLE_PRE poison —
//             Refined's own vanilla pre() can be consteval-bypassed on
//             GCC 16.1.1, so the helper guards explicitly); otherwise a
//             construction-time pre-check.
//
//   * Stages (pipeline depth) = the deepest pipeline the SMEM allows:
//             min( max-simultaneously-live-slot-count over the slot's
//                  [birth_op, death_op] window,         // live-set width
//                  smem_per_sm / nbytes,                // CostModel.h
//                  7 )                                  // CostModel.h
//             clamped to [1, 7].  The live-set width reuses the SAME
//             interval machinery the planner runs
//             (live_intervals_disjoint_at, MerkleDag.h:186) — birth/death
//             op intervals, external slots excluded.
//
// ─── Degradation (honest) ──────────────────────────────────────────
//
// Under DYNAMIC SHAPES (nbytes / live_width not compile constants) the
// derivation helpers stay correct but stop being zero-cost: the
// derive_pipeline_stages sweep runs at runtime and `derive_expect_tx`
// degrades from a compile constant to a construction-time CRUCIBLE_PRE.
// The derivation is correct in both regimes; only the *enforcement
// point* moves from compile time to establishment time.  Scope is never
// under-approximated — the V-273 gate (mem_scope_is_accel) refuses any
// non-accelerator scope outright, so a degraded plan can never silently
// drop to a too-weak fence.
//
// ─── Handle / lifetime note ────────────────────────────────────────
//
// The V-273 session's Resource is a pointer to an mbarrier-slot handle
// with a caller-side lifetime contract (SpscSession.h / V-273).  The
// real per-slot mbarrier backend is still stubbed through the CPU oracle
// (CLAUDE.md L0), so `mint_async_pipeline` takes the handle by reference
// (caller-owned, outlives the returned sessions) rather than constructing
// one internally — a locally-constructed handle would dangle the moment
// the sessions escaped.  The plan-only 3-arg form (plan, slot_id, ctx)
// becomes possible once a real arena-resident mbarrier backend lands
// (FIXY-V-275+); until then the handle is an explicit parameter.
//
// References:
//   sessions/AsyncPipelineSession.h — V-273 protocol + the two role mints.
//   MerkleDag.h — MemoryPlan, TensorSlot, live_intervals_disjoint_at.
//   CostModel.h — pipeline_stages ∈ [1,7], smem_per_sm.
//   fixy/Async.h — V-270 grant tags this facade composes with.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/MerkleDag.h>
#include <crucible/Platform.h>
#include <crucible/Types.h>
#include <crucible/algebra/lattices/MemoryScopeLattice.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/Pre.h>
#include <crucible/safety/Refined.h>
#include <crucible/sessions/AsyncPipelineSession.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

namespace crucible::fixy::async_pipeline {

namespace aps = ::crucible::safety::proto::async_pipeline_session;
using MemoryScope = ::crucible::algebra::lattices::MemoryScope;

// ── equals_slot_size<N> — scalar-equality Refined predicate ──────────
//
// RefinedAlgebra ships ExactSize<N> for CONTAINER element count
// (`c.size() == N`); this is the SCALAR analogue for a byte count
// (`v == N`), the mbarrier expect_tx ↔ planned-slot-size binding.
// Two pieces, mirroring Refined.h's Aligned<N>/aligned<N> convention:
// the STRUCT (the type) and the inline-constexpr VALUE (the NTTP).
template <std::size_t N>
struct EqualsSlotSize {
    [[nodiscard]] constexpr bool operator()(std::uint64_t v) const noexcept {
        return v == N;
    }
};

template <std::size_t N>
inline constexpr EqualsSlotSize<N> equals_slot_size{};

template <std::size_t N>
using ExpectTxBytes = ::crucible::safety::Refined<equals_slot_size<N>, std::uint64_t>;

// CostModel.h — KernelConfig::pipeline_stages valid range is [1, 7].
inline constexpr std::size_t kMaxPipelineStages = 7;

// ── Plan lookups (constexpr → consteval-usable on a constexpr plan) ──

[[nodiscard]] constexpr ::crucible::TensorSlot const*
find_slot(::crucible::MemoryPlan const& plan, ::crucible::SlotId slot_id) noexcept {
    for (std::uint32_t i = 0; i < plan.num_slots; ++i) {
        if (plan.slots[i].slot_id == slot_id) return &plan.slots[i];
    }
    return nullptr;
}

[[nodiscard]] constexpr std::uint64_t
slot_nbytes(::crucible::MemoryPlan const& plan, ::crucible::SlotId slot_id) noexcept {
    ::crucible::TensorSlot const* found = find_slot(plan, slot_id);
    return found != nullptr ? found->nbytes : std::uint64_t{0};
}

// ── Live-set width — the COUNT of simultaneously-live pool slots at op t.
//
// Mirrors the live-set FILTER inside live_intervals_disjoint_at
// (MerkleDag.h:194-205) but returns the cardinality instead of the
// disjointness verdict.  External slots are excluded (they keep their own
// allocations and never enter the pool / the pipeline).
[[nodiscard]] constexpr std::size_t
live_set_width_at(std::span<const ::crucible::TensorSlot> slots,
                  ::crucible::OpIndex op) noexcept {
    std::size_t width = 0;
    const std::uint32_t t = op.raw();
    for (::crucible::TensorSlot const& slot : slots) {
        if (slot.is_external) continue;
        if (slot.birth_op.raw() <= t && t <= slot.death_op.raw()) ++width;
    }
    return width;
}

// Max live-set width over the [birth, death] streaming window of a slot.
[[nodiscard]] constexpr std::size_t
max_live_width_over_window(std::span<const ::crucible::TensorSlot> slots,
                           ::crucible::OpIndex birth,
                           ::crucible::OpIndex death) noexcept {
    if (!birth.is_valid() || !death.is_valid()) return 1;
    std::size_t widest = 0;
    for (std::uint32_t t = birth.raw(); t <= death.raw(); ++t) {
        const std::size_t current = live_set_width_at(slots, ::crucible::OpIndex{t});
        if (current > widest) widest = current;
    }
    return widest;
}

// ── Stages derivation: min(live_width, smem_per_sm/nbytes, 7) ∈ [1,7] ─
[[nodiscard]] constexpr std::size_t
derive_pipeline_stages(::crucible::MemoryPlan const& plan,
                       ::crucible::SlotId slot_id,
                       std::uint32_t smem_per_sm) noexcept {
    ::crucible::TensorSlot const* found = find_slot(plan, slot_id);
    if (found == nullptr || found->nbytes == 0u) return 1;
    const std::size_t live_width = max_live_width_over_window(
        {plan.slots, plan.num_slots}, found->birth_op, found->death_op);
    const std::size_t smem_cap = static_cast<std::size_t>(smem_per_sm) / found->nbytes;
    std::size_t stages = live_width;
    if (smem_cap < stages) stages = smem_cap;
    if (kMaxPipelineStages < stages) stages = kMaxPipelineStages;
    if (stages < 1) stages = 1;
    return stages;
}

// ── Expect-tx binding (consteval on a constexpr plan; else runtime) ──
//
// Routes the value into the refinement type-system through the §XXI
// `mint_refined` factory (grep-discoverable).  The CRUCIBLE_PRE guard is
// the load-bearing check: it poisons the surrounding consteval call on
// mismatch (Refined's own vanilla pre() can be consteval-bypassed under
// GCC 16.1.1 — feedback_crucible_pre_post_macros), making a static-shape
// mismatch a COMPILE error and a dynamic-shape mismatch a construction
// abort.
template <std::size_t Bytes>
[[nodiscard]] constexpr ExpectTxBytes<Bytes>
derive_expect_tx(::crucible::MemoryPlan const& plan,
                 ::crucible::SlotId slot_id) noexcept {
    const std::uint64_t planned = slot_nbytes(plan, slot_id);
    CRUCIBLE_PRE(equals_slot_size<Bytes>(planned));
    return ::crucible::safety::mint_refined<equals_slot_size<Bytes>, std::uint64_t>(planned);
}

// ── Mint gate (§XXI single concept) ──────────────────────────────────
//
// Threads the V-273 handle gate (expect_tx-fits + accel-scope, which
// already implies Handle::stages >= 1) and adds: the ctx is a real
// ExecCtx, and the handle's pinned depth is a valid pipeline depth.
template <std::size_t Bytes, typename Handle, typename Ctx>
concept CtxFitsAsyncPipelineMint =
    aps::CtxFitsAsyncPipeline<Bytes, Handle>
    && ::crucible::effects::IsExecCtx<Ctx>
    && (static_cast<std::size_t>(Handle::stages) <= kMaxPipelineStages);

// ── Result: the producer + consumer session pair ─────────────────────
template <std::size_t Bytes, typename Handle, typename Ctx>
struct AsyncPipelinePair {
    aps::ProducerSessionHandle<Bytes, Handle, Ctx> producer;
    aps::ConsumerSessionHandle<Bytes, Handle, Ctx> consumer;
};

// ── mint_async_pipeline ──────────────────────────────────────────────
//
// §XXI ctx-bound mint: `Ctx const&` is the first parameter, the gate is
// the single concept CtxFitsAsyncPipelineMint.  Binds expect_tx via
// `Refined<equals_slot_size<Bytes>>` from the runtime MemoryPlan and
// validates the handle's pinned depth against the plan-derived bound,
// then mints the V-273 producer + consumer sessions over the
// (caller-owned) mbarrier slot handle.  constexpr per §XXI (no
// allocation — the plan is read through a borrowed pointer).
template <std::size_t Bytes, typename Handle, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsAsyncPipelineMint<Bytes, Handle, Ctx>
[[nodiscard]] constexpr auto
mint_async_pipeline(Ctx const& ctx,
                    ::crucible::MemoryPlan const* plan,
                    ::crucible::SlotId slot_id,
                    Handle& handle,
                    ::crucible::safety::Permission<typename Handle::slot_tag>&& slot_perm,
                    std::uint32_t smem_per_sm = 233472u) noexcept
    -> AsyncPipelinePair<Bytes, Handle, Ctx>
{
    CRUCIBLE_PRE(plan != nullptr);
    // expect_tx binding: the planned slot size MUST equal the compile-time
    // Bytes.  A mismatch aborts at establishment (compile error on a
    // constexpr plan) instead of hanging the consumer's try_wait forever.
    const ExpectTxBytes<Bytes> expect_tx = derive_expect_tx<Bytes>(*plan, slot_id);
    (void)expect_tx;
    // The handle's pinned depth must be the plan-derived deepest-pipeline.
    CRUCIBLE_PRE(static_cast<std::size_t>(Handle::stages)
                 == derive_pipeline_stages(*plan, slot_id, smem_per_sm));
    return AsyncPipelinePair<Bytes, Handle, Ctx>{
        aps::mint_async_pipeline_producer_session<Bytes>(
            ctx, handle, std::move(slot_perm)),
        aps::mint_async_pipeline_consumer_session<Bytes>(ctx, handle)};
}

}  // namespace crucible::fixy::async_pipeline

// ═══════════════════════════════════════════════════════════════════
// Compile-time witnesses
// ═══════════════════════════════════════════════════════════════════

namespace crucible::fixy::async_pipeline::detail::self_test {

// equals_slot_size predicate.
static_assert(equals_slot_size<256>(std::uint64_t{256}));
static_assert(!equals_slot_size<256>(std::uint64_t{128}));
static_assert(sizeof(ExpectTxBytes<256>) == sizeof(std::uint64_t));

// A constexpr two-slot plan: slot 0 lives [0,3] sized 256B; slot 1 lives
// [2,5] sized 256B.  The two windows overlap at ops 2,3 → max live width 2.
struct ConstevalPlanFixture {
    std::array<::crucible::TensorSlot, 2> slots{};
    ::crucible::MemoryPlan plan{};
    constexpr ConstevalPlanFixture() noexcept {
        slots[0] = ::crucible::TensorSlot{.offset_bytes = 0u, .nbytes = 256u,
            .birth_op = ::crucible::OpIndex{0u}, .death_op = ::crucible::OpIndex{3u},
            .slot_id = ::crucible::SlotId{0u}};
        slots[1] = ::crucible::TensorSlot{.offset_bytes = 256u, .nbytes = 256u,
            .birth_op = ::crucible::OpIndex{2u}, .death_op = ::crucible::OpIndex{5u},
            .slot_id = ::crucible::SlotId{1u}};
        plan.slots = slots.data();
        plan.num_slots = 2u;
    }
};

consteval std::uint64_t probe_nbytes() {
    ConstevalPlanFixture fixture{};
    return slot_nbytes(fixture.plan, ::crucible::SlotId{0u});
}
static_assert(probe_nbytes() == 256u);

consteval std::size_t probe_stages(std::uint32_t smem) {
    ConstevalPlanFixture fixture{};
    return derive_pipeline_stages(fixture.plan, ::crucible::SlotId{0u}, smem);
}
// Generous SMEM (228KB): bound by live-set width (2), not smem/256 (912) or 7.
static_assert(probe_stages(233472u) == 2u);
// Tight SMEM (256B): smem/256 == 1 dominates → clamped to 1.
static_assert(probe_stages(256u) == 1u);
// Mid SMEM exactly two slots' worth (512B): smem/256 == 2 == live width → 2.
static_assert(probe_stages(512u) == 2u);

// Consteval expect_tx binding succeeds when Bytes matches the plan.
consteval std::uint64_t probe_expect_tx_ok() {
    ConstevalPlanFixture fixture{};
    return derive_expect_tx<256>(fixture.plan, ::crucible::SlotId{0u}).into();
}
static_assert(probe_expect_tx_ok() == 256u);

}  // namespace crucible::fixy::async_pipeline::detail::self_test
