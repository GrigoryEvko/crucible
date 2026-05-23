#pragma once

// ═══════════════════════════════════════════════════════════════════
// AsyncPipelineSession.h — binary producer/consumer mbarrier pipeline
//                          as a SESSION-TYPED protocol (FIXY-V-273)
//
// GENERALIZES the one-shot ChainEdgeSession.h (Send<Signal, End> over
// the mimic semaphore oracle) to a LOOPING multi-stage protocol with a
// NON-EMPTY PermSet and a Linear payload.  This is the second production-
// shape wiring of FOUND-C v1's PermissionedSessionHandle stack (the
// first, SpscSession.h, deliberately stayed at EmptyPermSet + plain
// payloads — see its "What this wiring DOES NOT demonstrate" block).
// AsyncPipelineSession is exactly the richer wiring SpscSession points
// to: it threads a slot Permission through the wire via Transferable /
// Returned markers and watches the PermSet shrink-then-grow each loop.
//
// ─── The protocol ──────────────────────────────────────────────────
//
//   ProducerProto<SlotTag, Bytes> =
//       Loop< Send<Transferable<SmemFill<Bytes>,  SlotTag>,
//             Recv<Returned<   SmemDrain<Bytes>,  SlotTag>, Continue>>>
//   ConsumerProto<SlotTag, Bytes> = dual_of_t<ProducerProto<...>>
//                                 = Loop< Recv<Transferable<SmemFill>,
//                                         Send<Returned<SmemDrain>, Continue>>>
//
// MAPPING onto the hardware async-copy pipeline:
//   * producer Send<Transferable<…,SlotTag>>  ≙  mbarrier.arrive.expect_tx
//     after the TMA copy lands — it CONSUMES Permission<SlotTag> (the
//     producer hands the filled slot to the consumer).
//   * consumer Recv<Transferable<…,SlotTag>>  ≙  mbarrier.try_wait success
//     — it GAINS Permission<SlotTag> (the consumer now owns the slot).
//   * "slot drained, refillable" phase-flip back  ≙  the Returned edge:
//     consumer Send<Returned> surrenders SlotTag, producer Recv<Returned>
//     regains it, the Loop balances, and the next fill may proceed.
//
// ─── Why this is correct-by-construction (NOT by runtime check) ─────
//
//   PHASE CORRECTNESS — the mbarrier phase bit is NEVER modeled
//   numerically.  The Loop permission-balance assertion (every Continue
//   static_asserts perm_set_equal_v<PS, LoopEntryPS> in
//   PermissionedSession.h) IS the phase invariant.  Refilling a slot
//   before the consumer drained it ⇒ the producer reaches Continue still
//   missing SlotTag ⇒ perm_set_equal_v fails ⇒ COMPILE ERROR.
//
//   NO-DOUBLE-FILL — the slot is Linear<SmemSlot<Bytes,…>> (Linear.h
//   deleted-copy + -Werror=use-after-move), so a double consume() (a
//   second fill of a slot already in flight) is an INDEPENDENT value-
//   level compile error.  This is orthogonal to the permission-flow
//   guarantee: one guards the wire token, the other guards the value.
//
//   NO-LEAKED-SLOT — close() requires EmptyPermSet (PermissionedSession.h),
//   so a half-finished round trip that received a slot but never returned
//   it cannot reach a clean close.
//
//   DEADLOCK-FREEDOM — ConsumerProto is dual_of_t<ProducerProto> and both
//   are is_well_formed_v, the framework's compile-time duality + well-
//   formedness witness (Session.h).
//
// ─── Why the Resource is a POINTER ─────────────────────────────────
//
//   Same rationale as SpscSession.h: the slot handle is move-construct-
//   ible but not a Pinned lvalue the framework can store by reference,
//   so the blessed escape hatch is Resource = Handle* with a caller-side
//   lifetime contract (the pointee outlives the PSH).  The factories take
//   the handle by reference and internally take its address.
//
// ─── SCOPE LIMIT (honest) ──────────────────────────────────────────
//
//   BINARY single-producer / single-consumer ONLY.  Multi-warp
//   mbarrier.arrive (N threads incrementing a shared arrival count) is
//   genuinely MULTIPARTY and needs the MPST Offer / session_fork path;
//   that is DEFERRED to FIXY-V-275 and must NOT be forced onto this
//   binary shape.  The mimic semaphore_signal / poll oracle is reused
//   unchanged as the per-stage backend (it is still stubbed through the
//   CPU oracle until real vendor backends land — see CLAUDE.md L0).
//
// ─── References ────────────────────────────────────────────────────
//
//   sessions/SpscSession.h          — the EmptyPermSet streaming template.
//   sessions/ChainEdgeSession.h     — the one-shot semaphore session this
//                                     generalizes.
//   sessions/PermissionedSession.h  — FOUND-C v1 framework (PS evolution,
//                                     Loop balance, close() EmptyPermSet).
//   sessions/SessionPermPayloads.h  — Transferable / Returned PS arithmetic.
//   safety/ScopedFence.h            — V-267 MemoryScope grade on the slot.
//   test/test_permissioned_session_handle.cpp — the Transferable/Returned
//                                     PS shrink/grow integration test.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/algebra/lattices/MemoryScopeLattice.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/ScopedFence.h>
#include <crucible/sessions/PermissionedSession.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionGrade.h>
#include <crucible/sessions/SessionMint.h>
#include <crucible/sessions/SessionPermPayloads.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto::async_pipeline_session {

using MemoryScope = ::crucible::algebra::lattices::MemoryScope;
using ::crucible::algebra::lattices::mem_scope_is_accel;

// Re-export the permission-flow payload markers into this namespace so
// callers spell aps::Transferable / aps::Returned without reaching back
// into the parent proto namespace.
using ::crucible::safety::proto::Transferable;
using ::crucible::safety::proto::Returned;
using ::crucible::safety::proto::Borrowed;

// ── Wire descriptors ────────────────────────────────────────────────
//
// SmemFill is sent producer→consumer when a slot's TMA copy completes;
// SmemDrain is sent consumer→producer when the slot has been read out and
// is refillable.  Both carry the compile-time expect_tx byte count so the
// receiver can cross-check the transaction width, plus a runtime phase
// counter for diagnostics.  Trivial aggregates — valid Transferable /
// Returned payloads (move/copy constructible).

template <std::size_t Bytes>
struct SmemFill {
    static constexpr std::size_t expect_tx_bytes = Bytes;
    std::uint32_t phase = 0;
    [[nodiscard]] constexpr bool operator==(const SmemFill&) const noexcept = default;
};

template <std::size_t Bytes>
struct SmemDrain {
    static constexpr std::size_t expect_tx_bytes = Bytes;
    std::uint32_t phase = 0;
    [[nodiscard]] constexpr bool operator==(const SmemDrain&) const noexcept = default;
};

// ── Slot value (scope-graded, linear-held) ──────────────────────────
//
// SmemSlotData is the raw shared-memory ring slot.  SmemSlot wraps it in
// a V-267 ScopedFence<Scope, …> so the slot carries a MemoryScope grade
// (the visibility scope its fills publish at); LinearSmemSlot wraps THAT
// in Linear<> for the no-double-fill value guarantee.  The §XXI nesting
// is Linear (ownership, outer) ⊃ ScopedFence (MemoryScope grade) ⊃ raw
// data — the task's literal "Slot is Linear<SmemSlot<Bytes>>" with the
// scope grade carried by the nested ScopedFence.

template <std::size_t Bytes, std::size_t Stages>
struct SmemSlotData {
    static_assert(Bytes % 16 == 0,
                  "SmemSlotData<Bytes>: TMA expect_tx must be 16-byte aligned");
    static_assert(Stages >= 1,
                  "SmemSlotData<Stages>: pipeline depth must be >= 1");

    static constexpr std::size_t bytes  = Bytes;
    static constexpr std::size_t stages = Stages;

    std::uint32_t offset_bytes = 0;   // offset into the smem arena
    std::uint32_t phase        = 0;   // current ring phase

    [[nodiscard]] constexpr bool operator==(const SmemSlotData&) const noexcept = default;
};

template <std::size_t Bytes, std::size_t Stages, MemoryScope Scope>
using SmemSlot = ::crucible::safety::ScopedFence<Scope, SmemSlotData<Bytes, Stages>>;

template <std::size_t Bytes, std::size_t Stages, MemoryScope Scope>
using LinearSmemSlot = ::crucible::safety::Linear<SmemSlot<Bytes, Stages, Scope>>;

// ── Protocol shapes ─────────────────────────────────────────────────
//
// Infinite Loops: an async-copy pipeline streams indefinitely; shutdown
// is via detach (Loop without an exit branch — the documented infinite-
// loop pattern, same as SpscSession).  The producer starts holding the
// slot (InitialPS = PermSet<SlotTag>); the consumer starts empty.

template <typename SlotTag, std::size_t Bytes>
using ProducerProto =
    Loop<Send<Transferable<SmemFill<Bytes>, SlotTag>,
              Recv<Returned<SmemDrain<Bytes>, SlotTag>, Continue>>>;

template <typename SlotTag, std::size_t Bytes>
using ConsumerProto = dual_of_t<ProducerProto<SlotTag, Bytes>>;

// ── Substrate surface ───────────────────────────────────────────────
//
// The slot handle is "PermissionedChainEdge-style": it exposes the
// per-slot mbarrier oracle (arrive_expect_tx / try_wait) plus the
// compile-time slot identity (slot_tag, slot_bytes, stages, scope).
// A handle that omits any member, or whose ops have the wrong shape,
// fails the concept and the mint will not select.

template <typename Handle>
concept AsyncPipelineSlotHandle =
    requires {
        typename Handle::slot_tag;
        requires std::convertible_to<decltype(Handle::slot_bytes), std::size_t>;
        requires std::convertible_to<decltype(Handle::stages), std::size_t>;
        requires std::same_as<std::remove_cv_t<decltype(Handle::scope)>, MemoryScope>;
        requires (Handle::stages >= 1);
    } &&
    requires (Handle& handle, std::size_t bytes, std::uint32_t phase) {
        { handle.arrive_expect_tx(bytes) } -> std::same_as<void>;
        { handle.try_wait(phase) }         -> std::same_as<bool>;
    };

// ── Mint gate (§XXI single concept; two distinct mismatch classes) ──
//
//   class (a) — expect_tx mismatch: the protocol's declared Bytes must
//               equal the handle's slot_bytes capacity (an mbarrier
//               armed for N bytes cannot validate a copy of M != N).
//   class (b) — cross-trunk scope: an async-copy pipeline is a GPU
//               primitive, so the slot's scope must live in the accel
//               trunk (Warp..Gpu).  An ARM-shareability or Thread/System
//               scope is a wiring error and is rejected here.

template <std::size_t Bytes, typename Handle>
concept CtxFitsAsyncPipeline =
    AsyncPipelineSlotHandle<Handle>
    && (Bytes == static_cast<std::size_t>(Handle::slot_bytes))
    && mem_scope_is_accel(Handle::scope);

// ── Establishment factories (§XXI ctx-bound mints) ──────────────────
//
// Bytes is an explicit leading template param — the expect_tx the caller
// declares for this session.  FIXY-V-274's fixy/AsyncPipeline.h supplies
// it as a Refined<equals_slot_size> derived from the live MemoryPlan; the
// gate above rejects any value that disagrees with the handle.  SlotTag /
// Stages / Scope are carried by the Handle type and need not be respelled.

template <std::size_t Bytes,
          typename Handle,
          ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsAsyncPipeline<Bytes, Handle>
[[nodiscard]] constexpr auto
mint_async_pipeline_producer_session(
    Ctx const& ctx,
    Handle& handle,
    ::crucible::safety::Permission<typename Handle::slot_tag>&& slot_perm) noexcept
{
    using SlotTag = typename Handle::slot_tag;
    return mint_permissioned_session<ProducerProto<SlotTag, Bytes>>(
        ctx, &handle, std::move(slot_perm));
}

template <std::size_t Bytes,
          typename Handle,
          ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsAsyncPipeline<Bytes, Handle>
[[nodiscard]] constexpr auto
mint_async_pipeline_consumer_session(Ctx const& ctx, Handle& handle) noexcept
{
    using SlotTag = typename Handle::slot_tag;
    return mint_permissioned_session<ConsumerProto<SlotTag, Bytes>>(ctx, &handle);
}

// ── Session handle type aliases ─────────────────────────────────────

template <std::size_t Bytes,
          typename Handle,
          ::crucible::effects::IsExecCtx Ctx = ::crucible::effects::HotFgCtx>
using ProducerSessionHandle = decltype(
    mint_async_pipeline_producer_session<Bytes>(
        std::declval<Ctx const&>(),
        std::declval<Handle&>(),
        std::declval<::crucible::safety::Permission<typename Handle::slot_tag>&&>()));

template <std::size_t Bytes,
          typename Handle,
          ::crucible::effects::IsExecCtx Ctx = ::crucible::effects::HotFgCtx>
using ConsumerSessionHandle = decltype(
    mint_async_pipeline_consumer_session<Bytes>(
        std::declval<Ctx const&>(), std::declval<Handle&>()));

// ── Transport helpers ───────────────────────────────────────────────
//
// SEND transports are generic: PSH passes the rvalue payload, the helper
// drives the mbarrier op, and the payload's embedded Permission is
// consumed when the Transferable / Returned destructs at end-of-statement.
//
// RECV transports are TYPED factories: PSH's recv expects the helper to
// RETURN the protocol's exact payload (Transferable / Returned carrying a
// fresh Permission<SlotTag> — the recipient gains the slot).  A generic
// lambda cannot name that type, so the recipient side is parameterized on
// <Bytes, SlotTag>.  All four are oracle stubs: the real vendor backend
// derives the SlotTag permission from the hardware mbarrier phase rather
// than minting a root.

// Producer Send<Transferable<SmemFill>> : arm the mbarrier (expect_tx).
inline constexpr auto fill_send_transport = [](auto& hp, auto&& fill) noexcept {
    using FillT = std::remove_cvref_t<decltype(fill.value)>;
    hp->arrive_expect_tx(FillT::expect_tx_bytes);
};

// Consumer Send<Returned<SmemDrain>> : signal the slot is refillable.
inline constexpr auto drain_send_transport = [](auto& hp, auto&& drain) noexcept {
    using DrainT = std::remove_cvref_t<decltype(drain.value)>;
    hp->arrive_expect_tx(DrainT::expect_tx_bytes);
};

// Consumer Recv<Transferable<SmemFill>> : wait for the fill, gain SlotTag.
template <std::size_t Bytes, typename SlotTag>
[[nodiscard]] constexpr auto recv_fill_transport() noexcept {
    return [](auto& hp) noexcept {
        while (!hp->try_wait(0u)) { CRUCIBLE_SPIN_PAUSE; }
        return Transferable<SmemFill<Bytes>, SlotTag>{
            SmemFill<Bytes>{}, ::crucible::safety::mint_permission_root<SlotTag>()};
    };
}

// Producer Recv<Returned<SmemDrain>> : wait for the drain, regain SlotTag.
template <std::size_t Bytes, typename SlotTag>
[[nodiscard]] constexpr auto recv_drain_transport() noexcept {
    return [](auto& hp) noexcept {
        while (!hp->try_wait(0u)) { CRUCIBLE_SPIN_PAUSE; }
        return Returned<SmemDrain<Bytes>, SlotTag>{
            SmemDrain<Bytes>{}, ::crucible::safety::mint_permission_root<SlotTag>()};
    };
}

}  // namespace crucible::safety::proto::async_pipeline_session

// ═══════════════════════════════════════════════════════════════════
// Compile-time witnesses — fail-to-compile if the wiring drifts
// ═══════════════════════════════════════════════════════════════════

namespace crucible::safety::proto::async_pipeline_session::detail::self_test {

struct SlotTag {};

// A minimal mbarrier slot handle satisfying AsyncPipelineSlotHandle.
struct FakeSlotHandle {
    using slot_tag = SlotTag;
    static constexpr std::size_t slot_bytes = 256;
    static constexpr std::size_t stages     = 2;
    static constexpr MemoryScope scope      = MemoryScope::Cta;

    std::uint32_t arrivals = 0;
    constexpr void arrive_expect_tx(std::size_t) noexcept { ++arrivals; }
    [[nodiscard]] constexpr bool try_wait(std::uint32_t) noexcept { return true; }
};

static_assert(AsyncPipelineSlotHandle<FakeSlotHandle>);
static_assert(CtxFitsAsyncPipeline<256, FakeSlotHandle>);

// ── Protocol-shape witnesses ────────────────────────────────────────
using Prod = ProducerProto<SlotTag, 256>;
using Cons = ConsumerProto<SlotTag, 256>;

static_assert(std::is_same_v<Prod,
    Loop<Send<Transferable<SmemFill<256>, SlotTag>,
              Recv<Returned<SmemDrain<256>, SlotTag>, Continue>>>>);
static_assert(std::is_same_v<Cons,
    Loop<Recv<Transferable<SmemFill<256>, SlotTag>,
              Send<Returned<SmemDrain<256>, SlotTag>, Continue>>>>);
static_assert(std::is_same_v<dual_of_t<Prod>, Cons>,
              "AsyncPipeline producer/consumer must be exact duals "
              "(deadlock-freedom witness)");
static_assert(std::is_same_v<dual_of_t<Cons>, Prod>);

// ── Slot value witnesses ────────────────────────────────────────────
using Slot = LinearSmemSlot<256, 2, MemoryScope::Cta>;
static_assert(std::is_same_v<Slot,
    ::crucible::safety::Linear<
        ::crucible::safety::ScopedFence<MemoryScope::Cta, SmemSlotData<256, 2>>>>);
static_assert(SmemSlotData<256, 2>::bytes == 256);
static_assert(SmemSlotData<256, 2>::stages == 2);

// ── Established session-handle witnesses ────────────────────────────
using ProdSession = ProducerSessionHandle<256, FakeSlotHandle>;
using ConsSession = ConsumerSessionHandle<256, FakeSlotHandle>;

// Producer's loop body enters with PS = PermSet<SlotTag> (it holds the
// slot); consumer enters with EmptyPermSet (it does not).
static_assert(std::is_same_v<typename ProdSession::perm_set, PermSet<SlotTag>>,
              "producer session must start holding the slot permission");
static_assert(std::is_same_v<typename ConsSession::perm_set, EmptyPermSet>,
              "consumer session must start without the slot permission");

// ── PS-evolution witnesses (the phase invariant, statically) ────────
//
// Producer body: Send<Transferable<…,SlotTag>> removes SlotTag (slot
// handed off); Recv<Returned<…,SlotTag>> re-inserts it; the Loop balances.
static_assert(perm_set_equal_v<
    compute_perm_set_after_send_t<PermSet<SlotTag>,
                                  Transferable<SmemFill<256>, SlotTag>>,
    EmptyPermSet>,
    "Send<Transferable<…,SlotTag>> must drop SlotTag from the producer PS");
static_assert(perm_set_equal_v<
    compute_perm_set_after_recv_t<EmptyPermSet,
                                  Returned<SmemDrain<256>, SlotTag>>,
    PermSet<SlotTag>>,
    "Recv<Returned<…,SlotTag>> must restore SlotTag to the producer PS");

}  // namespace crucible::safety::proto::async_pipeline_session::detail::self_test
