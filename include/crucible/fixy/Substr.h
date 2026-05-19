#pragma once

// ── crucible::fixy::substr — Substrate session minters ─────────────
//
// Phase C re-export per misc/16_05_2026_fixy.md.  Surfaces the
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
// ── Raw concurrent primitives without a typed-session layer ────────
// ═════════════════════════════════════════════════════════════════════
//
// Substrates that ship a `concurrent::Permissioned*` primitive but
// have not yet been wrapped by a `sessions/*Session.h` typed-session
// header.  Surfaced under `fixy::substr::concurrent::*` so callers
// who need the raw substrate (handle-level only, no session protocol)
// can reach it through the fixy umbrella.
//
// MPSC: raw substrate (concurrent/PermissionedMpscChannel.h) is
// surfaced below.  No `sessions/MpscChannelSession.h` ships today;
// the related fixy-side gap (a `fixy::substr::mpsc` sub-namespace
// mirroring `fixy::substr::spsc::` / `metalog::` / etc.) is tracked
// independently as fixy-M-20 — fixy-A4-028 deliberately does NOT
// promise a delivery here, because the substrate-side typed-session
// header is the actual prerequisite and its shape is unsettled
// (MPMC ships via MpmcChannelSession.h with a different protocol
// shape that may or may not generalize cleanly to MPSC).

namespace concurrent {
// MPSC channel (multi-producer single-consumer) — raw primitive only.
template <::crucible::concurrent::RingValue T,
          std::size_t Capacity,
          typename UserTag = void>
using PermissionedMpscChannel =
    ::crucible::concurrent::PermissionedMpscChannel<T, Capacity, UserTag>;
}  // namespace concurrent

// ── fixy-A4-005 + fixy-M-19: mint_substrate_session — the generic
// substrate→session bridge.  Lives in `concurrent::` (Tier-2→3 bridge
// per §XXI); the canonical homes in fixy/ are HERE in `substr::` (next
// to every per-substrate sub-namespace's own `mint_*_session` family)
// AND in `sess::` (result-side: it produces a Session handle).  The
// `pipe::` re-export was a grace-window misplacement; M-19 closed it.
using ::crucible::concurrent::mint_substrate_session;

}  // namespace crucible::fixy::substr
