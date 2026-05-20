#pragma once

// ── crucible::fixy::substr — Substrate session minters ─────────────
//
// Re-export per misc/16_05_2026_fixy.md.  Surfaces the
// per-substrate typed-session factories (SPSC, SWMR, Snapshot,
// ChaseLev, MetaLog, ChainEdge, MPMC, CalendarGrid,
// ShardedCalendarGrid, ShardedGrid) under `fixy::substr::` so callers
// who include only the fixy umbrella never have to pick the right
// sessions/* header for the substrate they hold a handle into.
//
// Per CLAUDE.md §XXI Universal Mint Pattern: every re-export
// preserves the substrate's structural concept gate (the substrate's
// own handle types are the gate), the `[[nodiscard]] noexcept`
// qualifiers, and the producer/consumer (or owner/thief / writer/
// reader / signaler/waiter) duality.
//
// Each substrate exposes two flavors:
//   - Endpoint mint (no Ctx): substrate-shape mint of a typed
//     producer/consumer/writer/reader handle.
//   - Session mint (Ctx-bound): wraps the endpoint handle in a
//     PermissionedSessionHandle for cross-tier composition.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   SpscSession.h         producer / consumer + _session variants
//   SwmrSession.h         writer / reader + _session + _runtime_session
//   SnapshotSession.h     snapshot_writer / snapshot_reader +
//                         _session variants (over PermissionedSnapshot)
//   ChaseLevDequeSession.h chaselev_owner / chaselev_thief +
//                         owner / thief _session
//   MetaLogSession.h      metalog_producer / metalog_consumer +
//                         _session variants
//   ChainEdgeSession.h    chainedge_signaler / chainedge_waiter +
//                         _session variants
//   MpmcChannelSession.h  mpmc_producer_endpoint /
//                         mpmc_consumer_endpoint + _session variants
//   PermissionedMpscChannel.h  (concurrent/, no session header)
//                         mpsc_producer_endpoint / mpsc_consumer_endpoint
//                         + producer / consumer _session — shims defined
//                         directly in `mpsc::` since the substrate ships
//                         no sessions/MpscChannelSession.h.  The protocol
//                         shape is identical to the canonical
//                         SubstrateSessionBridge default_proto_for<>
//                         specialization for PermissionedMpscChannel.
//   CalendarGridSession.h calendar_grid_producer / _consumer +
//                         producer / consumer _session
//   ShardedCalendarGridSession.h  sharded_calendar_grid_*
//   ShardedGridSession.h  sharded_grid_*
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — re-exports introduce no new state path.
//   TypeSafe — using-declarations preserve substrate's handle
//              wrapper types and ctx-bound concept gates.
//   NullSafe — handles inherit substrate's pointer discipline.
//   MemSafe  — substrate handles are RAII; alias preserves.
//   BorrowSafe — duality (producer ⊥ consumer, owner ⊥ thief) is
//              substrate-level; alias passes through.
//   ThreadSafe — substrate handles' atomic discipline carries through.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  using-declarations are pure name-lookup directives.

#include <crucible/concurrent/PermissionedMpscChannel.h>
#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/concurrent/SubstrateSessionBridge.h>
#include <crucible/sessions/CalendarGridSession.h>
#include <crucible/sessions/ChainEdgeSession.h>
#include <crucible/sessions/ChaseLevDequeSession.h>
#include <crucible/sessions/MetaLogSession.h>
#include <crucible/sessions/MpmcChannelSession.h>
#include <crucible/sessions/ShardedCalendarGridSession.h>
#include <crucible/sessions/ShardedGridSession.h>
#include <crucible/sessions/SnapshotSession.h>
#include <crucible/sessions/SpscSession.h>
#include <crucible/sessions/SwmrSession.h>

#include <type_traits>  // FIXY-U-103 sentinel uses std::is_same_v

namespace crucible::fixy::substr {

// ═════════════════════════════════════════════════════════════════════
// ── SPSC ──────────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace spsc {
// Protocol type aliases (callers need these to spell out the session
// handle type at variable / struct-field declaration sites).
template <typename T>
using ProducerProto =
    ::crucible::safety::proto::spsc_session::ProducerProto<T>;

template <typename T>
using ConsumerProto =
    ::crucible::safety::proto::spsc_session::ConsumerProto<T>;

// Mint factories.
using ::crucible::safety::proto::spsc_session::mint_producer_session;
using ::crucible::safety::proto::spsc_session::mint_consumer_session;
}  // namespace spsc

// ═════════════════════════════════════════════════════════════════════
// ── SWMR (single-writer multi-reader) ──────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace swmr {
// Protocol type aliases.
template <typename T>
using WriterProto =
    ::crucible::safety::proto::swmr_session::WriterProto<T>;

template <typename T, typename ReaderTag>
using ReaderProto =
    ::crucible::safety::proto::swmr_session::ReaderProto<T, ReaderTag>;

template <typename T>
using WriterRuntimeProto =
    ::crucible::safety::proto::swmr_session::WriterRuntimeProto<T>;

template <typename T>
using ReaderRuntimeProto =
    ::crucible::safety::proto::swmr_session::ReaderRuntimeProto<T>;

// Surface concept.
template <typename S>
concept SwmrSessionSurface =
    ::crucible::safety::proto::swmr_session::SwmrSessionSurface<S>;

// SwmrSession owning aggregate.
template <typename T, typename WriterTag, typename ReaderTag>
using SwmrSession =
    ::crucible::safety::proto::swmr_session::SwmrSession<T, WriterTag, ReaderTag>;

// Mint factories.
using ::crucible::safety::proto::swmr_session::mint_swmr_writer;
using ::crucible::safety::proto::swmr_session::mint_swmr_reader;
using ::crucible::safety::proto::swmr_session::mint_writer_session;
using ::crucible::safety::proto::swmr_session::mint_reader_session;
using ::crucible::safety::proto::swmr_session::mint_writer_runtime_session;
using ::crucible::safety::proto::swmr_session::mint_reader_runtime_session;
}  // namespace swmr

// ═════════════════════════════════════════════════════════════════════
// ── Chase-Lev deque (work-stealing) ────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace chaselev {
// Protocol type aliases.
template <typename T>
using OwnerProto =
    ::crucible::safety::proto::chaselev_session::OwnerProto<T>;

template <typename T, typename ThiefTag>
using ThiefProto =
    ::crucible::safety::proto::chaselev_session::ThiefProto<T, ThiefTag>;

// Surface concept.
template <typename Deque>
concept ChaseLevSessionSurface =
    ::crucible::safety::proto::chaselev_session::ChaseLevSessionSurface<Deque>;

// Mint factories.
using ::crucible::safety::proto::chaselev_session::mint_chaselev_owner;
using ::crucible::safety::proto::chaselev_session::mint_chaselev_thief;
using ::crucible::safety::proto::chaselev_session::mint_owner_session;
using ::crucible::safety::proto::chaselev_session::mint_thief_session;
}  // namespace chaselev

// ═════════════════════════════════════════════════════════════════════
// ── MetaLog (tensor-metadata side channel) ─────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace metalog {
// Record type + protocol aliases.
using MetaLogRecord =
    ::crucible::safety::proto::metalog_session::MetaLogRecord;

using ProducerProto =
    ::crucible::safety::proto::metalog_session::ProducerProto;

using ConsumerProto =
    ::crucible::safety::proto::metalog_session::ConsumerProto;

// Surface concept.
template <typename Log>
concept MetaLogSessionSurface =
    ::crucible::safety::proto::metalog_session::MetaLogSessionSurface<Log>;

// Mint factories.
using ::crucible::safety::proto::metalog_session::mint_metalog_producer;
using ::crucible::safety::proto::metalog_session::mint_metalog_consumer;
using ::crucible::safety::proto::metalog_session::mint_metalog_producer_session;
using ::crucible::safety::proto::metalog_session::mint_metalog_consumer_session;
}  // namespace metalog

// ═════════════════════════════════════════════════════════════════════
// ── ChainEdge (execution-plan semaphore one-shot) ──────────────────
// ═════════════════════════════════════════════════════════════════════

namespace chainedge {
// Signal type + protocol aliases.
using Signal = ::crucible::safety::proto::chainedge_session::Signal;
using SignalerProto =
    ::crucible::safety::proto::chainedge_session::SignalerProto;
using WaiterProto =
    ::crucible::safety::proto::chainedge_session::WaiterProto;

// Surface concept.
template <typename Edge>
concept ChainEdgeSessionSurface =
    ::crucible::safety::proto::chainedge_session::ChainEdgeSessionSurface<Edge>;

// Mint factories.
using ::crucible::safety::proto::chainedge_session::mint_chainedge_signaler;
using ::crucible::safety::proto::chainedge_session::mint_chainedge_waiter;
using ::crucible::safety::proto::chainedge_session::mint_chainedge_signaler_session;
using ::crucible::safety::proto::chainedge_session::mint_chainedge_waiter_session;
}  // namespace chainedge

// ═════════════════════════════════════════════════════════════════════
// ── MPMC channel ───────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace mpmc {
// Protocol type aliases.
template <typename T>
using ProducerProto =
    ::crucible::safety::proto::mpmc_channel_session::ProducerProto<T>;

template <typename T>
using ConsumerProto =
    ::crucible::safety::proto::mpmc_channel_session::ConsumerProto<T>;

// Surface concept.
template <typename Channel>
concept MpmcChannelSessionSurface =
    ::crucible::safety::proto::mpmc_channel_session::MpmcChannelSessionSurface<Channel>;

// Mint factories.
using ::crucible::safety::proto::mpmc_channel_session::mint_mpmc_producer_endpoint;
using ::crucible::safety::proto::mpmc_channel_session::mint_mpmc_consumer_endpoint;
using ::crucible::safety::proto::mpmc_channel_session::mint_mpmc_producer_session;
using ::crucible::safety::proto::mpmc_channel_session::mint_mpmc_consumer_session;
}  // namespace mpmc

// ═════════════════════════════════════════════════════════════════════
// ── Calendar grid (per-cycle producer/consumer) ────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace calendar_grid {
// Protocol type aliases.
template <typename T>
using ProducerProto =
    ::crucible::safety::proto::calendar_grid_session::ProducerProto<T>;

template <typename T>
using ConsumerProto =
    ::crucible::safety::proto::calendar_grid_session::ConsumerProto<T>;

// Surface concept.
template <typename Grid>
concept CalendarGridSessionSurface =
    ::crucible::safety::proto::calendar_grid_session::CalendarGridSessionSurface<Grid>;

// Mint factories.
using ::crucible::safety::proto::calendar_grid_session::mint_calendar_grid_producer;
using ::crucible::safety::proto::calendar_grid_session::mint_calendar_grid_consumer;
using ::crucible::safety::proto::calendar_grid_session::mint_producer_session;
using ::crucible::safety::proto::calendar_grid_session::mint_consumer_session;
}  // namespace calendar_grid

// ═════════════════════════════════════════════════════════════════════
// ── Sharded calendar grid ──────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace sharded_calendar_grid {
// Protocol type aliases.
template <typename T>
using ProducerProto =
    ::crucible::safety::proto::sharded_calendar_grid_session::ProducerProto<T>;

template <typename T>
using ConsumerProto =
    ::crucible::safety::proto::sharded_calendar_grid_session::ConsumerProto<T>;

// Surface concept.
template <typename Grid>
concept ShardedCalendarGridSessionSurface =
    ::crucible::safety::proto::sharded_calendar_grid_session::ShardedCalendarGridSessionSurface<Grid>;

// Mint factories.
using ::crucible::safety::proto::sharded_calendar_grid_session::mint_sharded_calendar_grid_producer;
using ::crucible::safety::proto::sharded_calendar_grid_session::mint_sharded_calendar_grid_consumer;
using ::crucible::safety::proto::sharded_calendar_grid_session::mint_producer_session;
using ::crucible::safety::proto::sharded_calendar_grid_session::mint_consumer_session;
}  // namespace sharded_calendar_grid

// ═════════════════════════════════════════════════════════════════════
// ── Sharded grid ───────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace sharded_grid {
// Protocol type aliases.
template <typename T>
using ProducerProto =
    ::crucible::safety::proto::sharded_grid_session::ProducerProto<T>;

template <typename T>
using ConsumerProto =
    ::crucible::safety::proto::sharded_grid_session::ConsumerProto<T>;

// Surface concept.
template <typename Grid>
concept ShardedGridSessionSurface =
    ::crucible::safety::proto::sharded_grid_session::ShardedGridSessionSurface<Grid>;

// Mint factories.
using ::crucible::safety::proto::sharded_grid_session::mint_sharded_grid_producer;
using ::crucible::safety::proto::sharded_grid_session::mint_sharded_grid_consumer;
using ::crucible::safety::proto::sharded_grid_session::mint_producer_session;
using ::crucible::safety::proto::sharded_grid_session::mint_consumer_session;
}  // namespace sharded_grid

// ═════════════════════════════════════════════════════════════════════
// ── Snapshot (PermissionedSnapshot SWMR seqlock) ───────────────────
// ═════════════════════════════════════════════════════════════════════

namespace snapshot {
// Substrate alias (raw primitive — exposed alongside the session
// surface because short-lived readers reach the substrate directly).
template <::crucible::concurrent::SnapshotValue T,
          typename UserTag = void>
using PermissionedSnapshot =
    ::crucible::concurrent::PermissionedSnapshot<T, UserTag>;

// Protocol type aliases.
template <typename T>
using WriterProto =
    ::crucible::safety::proto::snapshot_session::WriterProto<T>;

template <typename T>
using ReaderProto =
    ::crucible::safety::proto::snapshot_session::ReaderProto<T>;

// Surface concept.
template <typename Snap>
concept SnapshotSessionSurface =
    ::crucible::safety::proto::snapshot_session::SnapshotSessionSurface<Snap>;

// Mint factories.
using ::crucible::safety::proto::snapshot_session::mint_snapshot_writer;
using ::crucible::safety::proto::snapshot_session::mint_snapshot_reader;
using ::crucible::safety::proto::snapshot_session::mint_snapshot_writer_session;
using ::crucible::safety::proto::snapshot_session::mint_snapshot_reader_session;
}  // namespace snapshot

// ═════════════════════════════════════════════════════════════════════
// ── MPSC channel (multi-producer single-consumer) ──────────────────
// ═════════════════════════════════════════════════════════════════════
//
// MPSC ships a `concurrent/PermissionedMpscChannel.h` substrate but
// no `sessions/MpscChannelSession.h` typed-session header — MPMC's
// shape (both endpoints Pool-mediated) does not generalize cleanly
// to MPSC (Linear consumer Permission + fractional producer Pool).
// Rather than wait for a session header that may never ship in this
// shape, the fixy umbrella defines the mpsc::* surface here, directly
// against the substrate primitive.  Mirror of the spsc:: / swmr:: /
// mpmc:: pattern: ProducerProto / ConsumerProto type aliases, surface
// concept, endpoint mints, ctx-bound session mints.  Closes fixy-M-20.
//
// Protocol shape matches the canonical SubstrateSessionBridge
// default_proto_for<PermissionedMpscChannel<...>, Direction::*>
// specialization exactly (Loop<Send<T,Continue>> for the producer
// stream; Loop<Recv<T,Continue>> for the consumer drain).  The bridge
// already supports `mint_substrate_session<MpscT, Direction::Producer>`
// and `Direction::Consumer`; mpsc:: surfaces named-MPSC equivalents
// for callers who prefer the substrate-named factory style and adds
// a surface concept gate so MPSC-specific call sites get a
// substrate-typed diagnostic instead of a generic substrate-bridge
// concept failure.

namespace mpsc {
// Substrate alias — exposed because callers reach the substrate
// directly when minting endpoints (no two-step session-header indirection
// exists yet for MPSC).
template <::crucible::concurrent::RingValue T,
          std::size_t Capacity,
          typename UserTag = void>
using PermissionedMpscChannel =
    ::crucible::concurrent::PermissionedMpscChannel<T, Capacity, UserTag>;

// Protocol type aliases — match default_proto_for<MpscT, Direction::*>.
template <typename T>
using ProducerProto =
    ::crucible::safety::proto::Loop<
        ::crucible::safety::proto::Send<T,
            ::crucible::safety::proto::Continue>>;

template <typename T>
using ConsumerProto =
    ::crucible::safety::proto::Loop<
        ::crucible::safety::proto::Recv<T,
            ::crucible::safety::proto::Continue>>;

// Surface concept — gates the mint factories below.  Differs from
// MpmcChannelSessionSurface in two ways:
//   1. `ch.consumer(perm)` takes a Linear Permission&& and returns a
//      bare ConsumerHandle (NOT optional); MpscRing's try_pop is
//      single-consumer-only, so the consumer side is structurally
//      Linear, not Pool-mediated.
//   2. `ch.producer()` IS Pool-mediated and returns optional (matches
//      MPMC's producer side).
template <typename Channel>
concept MpscChannelSessionSurface = requires(
    Channel& ch,
    typename Channel::ProducerHandle& producer_handle,
    typename Channel::ConsumerHandle& consumer_handle,
    ::crucible::safety::Permission<typename Channel::consumer_tag>&& cons_perm,
    const typename Channel::value_type& sample_payload)
{
    typename Channel::value_type;
    typename Channel::user_tag;
    typename Channel::producer_tag;
    typename Channel::consumer_tag;
    typename Channel::ProducerHandle;
    typename Channel::ConsumerHandle;

    // Endpoint factories — producer Pool-mediated, consumer Linear.
    { ch.producer() }
        -> std::same_as<std::optional<typename Channel::ProducerHandle>>;
    { ch.consumer(std::move(cons_perm)) }
        -> std::same_as<typename Channel::ConsumerHandle>;

    // Handle method shapes.
    { producer_handle.try_push(sample_payload) }
        -> std::same_as<bool>;
    { consumer_handle.try_pop() }
        -> std::same_as<std::optional<typename Channel::value_type>>;
};

// Endpoint mint helpers — pure forwarders to the substrate's
// .producer() / .consumer() member factories.  Surfaced here so
// every per-substrate `mpsc::mint_*_endpoint` follows the same
// `mint_<substrate>_<role>_endpoint(ch[, perm])` shape grep-finds.

template <MpscChannelSessionSurface Channel>
[[nodiscard]] constexpr auto mint_mpsc_producer_endpoint(Channel& ch) noexcept
    -> std::optional<typename Channel::ProducerHandle>
{
    return ch.producer();
}

template <MpscChannelSessionSurface Channel>
[[nodiscard]] constexpr auto mint_mpsc_consumer_endpoint(
    Channel& ch,
    ::crucible::safety::Permission<typename Channel::consumer_tag>&& perm) noexcept
    -> typename Channel::ConsumerHandle
{
    return ch.consumer(std::move(perm));
}

// Session mint factories (ctx-bound) — wrap the endpoint handle in
// a PermissionedSessionHandle.  Take handle BY REFERENCE; internally
// take its address and forward as Resource = Handle* per the MPMC /
// MetaLog precedent.  EmptyPermSet because MPSC's wire-permission
// flow is empty — producer/consumer authority lives in the embedded
// SharedPermissionGuard / Linear Permission on the handle itself.

template <MpscChannelSessionSurface Channel,
          ::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] constexpr auto
mint_mpsc_producer_session(Ctx const& ctx,
                           typename Channel::ProducerHandle& handle) noexcept
{
    using T = typename Channel::value_type;
    return ::crucible::safety::proto::mint_permissioned_session<
        ProducerProto<T>>(ctx, &handle);
}

template <MpscChannelSessionSurface Channel,
          ::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] constexpr auto
mint_mpsc_consumer_session(Ctx const& ctx,
                           typename Channel::ConsumerHandle& handle) noexcept
{
    using T = typename Channel::value_type;
    return ::crucible::safety::proto::mint_permissioned_session<
        ConsumerProto<T>>(ctx, &handle);
}
}  // namespace mpsc

// ── fixy-A4-005 + fixy-M-19: mint_substrate_session — the generic
// substrate→session bridge.  Lives in `concurrent::` (Tier-2→3 bridge
// per §XXI); the canonical homes in fixy/ are HERE in `substr::` (next
// to every per-substrate sub-namespace's own `mint_*_session` family)
// AND in `sess::` (result-side: it produces a Session handle).  The
// `pipe::` re-export was a grace-window misplacement; M-19 closed it.
using ::crucible::concurrent::mint_substrate_session;

}  // namespace crucible::fixy::substr

// ─── FIXY-U-103 in-header sentinel ─────────────────────────────────
//
// Drift-catch for the per-substrate sub-namespaces: spsc (2), swmr (6),
// chaselev (4), metalog (4), chainedge (4), mpmc (4), calendar_grid (4),
// sharded_calendar_grid (4), sharded_grid (4), snapshot (4), and the
// outer-level mint_substrate_session re-export (1).  mpsc:: is NOT a
// pure re-export — it defines its own mint factories (also covered by
// dedicated test_fixy_substr_mpsc fixtures) so it doesn't contribute
// to the using-decl cardinality.
//
// Same recipe as fixy/Pipe.h / fixy/Struct.h: type-identity witnesses
// for representative items + per-sub-namespace cardinality mirrors.
//
// FIXY-U-103.

namespace crucible::fixy::substr::self_test {

// ── Representative type-identity witnesses ───────────────────────

// MetaLog non-template type-aliases preserve substrate identity.
static_assert(std::is_same_v<
    ::crucible::fixy::substr::metalog::MetaLogRecord,
    ::crucible::safety::proto::metalog_session::MetaLogRecord>,
    "fixy::substr::metalog::MetaLogRecord must alias the substrate.");

static_assert(std::is_same_v<
    ::crucible::fixy::substr::metalog::ProducerProto,
    ::crucible::safety::proto::metalog_session::ProducerProto>,
    "fixy::substr::metalog::ProducerProto must alias the substrate.");

// ChainEdge non-template aliases preserve substrate identity.
static_assert(std::is_same_v<
    ::crucible::fixy::substr::chainedge::Signal,
    ::crucible::safety::proto::chainedge_session::Signal>,
    "fixy::substr::chainedge::Signal must alias the substrate.");

static_assert(std::is_same_v<
    ::crucible::fixy::substr::chainedge::SignalerProto,
    ::crucible::safety::proto::chainedge_session::SignalerProto>,
    "fixy::substr::chainedge::SignalerProto must alias the substrate.");

// SPSC template alias preserves identity (parameterized witness).
static_assert(std::is_same_v<
    ::crucible::fixy::substr::spsc::ProducerProto<int>,
    ::crucible::safety::proto::spsc_session::ProducerProto<int>>,
    "fixy::substr::spsc::ProducerProto<T> must alias the substrate.");

// ── Per-sub-namespace cardinality witnesses ──────────────────────

constexpr int substr_spsc_using                  = 2;
constexpr int substr_swmr_using                  = 6;
constexpr int substr_chaselev_using              = 4;
constexpr int substr_metalog_using               = 4;
constexpr int substr_chainedge_using             = 4;
constexpr int substr_mpmc_using                  = 4;
constexpr int substr_calendar_grid_using         = 4;
constexpr int substr_sharded_calendar_grid_using = 4;
constexpr int substr_sharded_grid_using          = 4;
constexpr int substr_snapshot_using              = 4;
constexpr int substr_outer_using                 = 1;

constexpr int substr_total_using =
    substr_spsc_using + substr_swmr_using + substr_chaselev_using +
    substr_metalog_using + substr_chainedge_using + substr_mpmc_using +
    substr_calendar_grid_using + substr_sharded_calendar_grid_using +
    substr_sharded_grid_using + substr_snapshot_using +
    substr_outer_using;

static_assert(substr_total_using == 41,
    "fixy::substr:: using-decl surface drifted from 41 — Substr.h "
    "sub-namespace re-exports and this sentinel must update in lockstep.");

}  // namespace crucible::fixy::substr::self_test
