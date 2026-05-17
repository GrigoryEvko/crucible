#pragma once

// ── crucible::fixy::substr — Substrate session minters ─────────────
//
// Phase C re-export per misc/16_05_2026_fixy.md.  Surfaces the
// per-substrate typed-session factories (SPSC, SWMR, ChaseLev,
// MetaLog, ChainEdge, MPMC, CalendarGrid, ShardedCalendarGrid,
// ShardedGrid) under `fixy::substr::` so callers who include only
// the fixy umbrella never have to pick the right sessions/* header
// for the substrate they hold a handle into.
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

#include <crucible/sessions/CalendarGridSession.h>
#include <crucible/sessions/ChainEdgeSession.h>
#include <crucible/sessions/ChaseLevDequeSession.h>
#include <crucible/sessions/MetaLogSession.h>
#include <crucible/sessions/MpmcChannelSession.h>
#include <crucible/sessions/ShardedCalendarGridSession.h>
#include <crucible/sessions/ShardedGridSession.h>
#include <crucible/sessions/SpscSession.h>
#include <crucible/sessions/SwmrSession.h>

namespace crucible::fixy::substr {

// ═════════════════════════════════════════════════════════════════════
// ── SPSC ──────────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace spsc {
using ::crucible::safety::proto::spsc_session::mint_producer_session;
using ::crucible::safety::proto::spsc_session::mint_consumer_session;
}  // namespace spsc

// ═════════════════════════════════════════════════════════════════════
// ── SWMR (single-writer multi-reader) ──────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace swmr {
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
using ::crucible::safety::proto::chaselev_session::mint_chaselev_owner;
using ::crucible::safety::proto::chaselev_session::mint_chaselev_thief;
using ::crucible::safety::proto::chaselev_session::mint_owner_session;
using ::crucible::safety::proto::chaselev_session::mint_thief_session;
}  // namespace chaselev

// ═════════════════════════════════════════════════════════════════════
// ── MetaLog (tensor-metadata side channel) ─────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace metalog {
using ::crucible::safety::proto::metalog_session::mint_metalog_producer;
using ::crucible::safety::proto::metalog_session::mint_metalog_consumer;
using ::crucible::safety::proto::metalog_session::mint_metalog_producer_session;
using ::crucible::safety::proto::metalog_session::mint_metalog_consumer_session;
}  // namespace metalog

// ═════════════════════════════════════════════════════════════════════
// ── ChainEdge (execution-plan semaphore one-shot) ──────────────────
// ═════════════════════════════════════════════════════════════════════

namespace chainedge {
using ::crucible::safety::proto::chainedge_session::mint_chainedge_signaler;
using ::crucible::safety::proto::chainedge_session::mint_chainedge_waiter;
using ::crucible::safety::proto::chainedge_session::mint_chainedge_signaler_session;
using ::crucible::safety::proto::chainedge_session::mint_chainedge_waiter_session;
}  // namespace chainedge

// ═════════════════════════════════════════════════════════════════════
// ── MPMC channel ───────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace mpmc {
using ::crucible::safety::proto::mpmc_channel_session::mint_mpmc_producer_endpoint;
using ::crucible::safety::proto::mpmc_channel_session::mint_mpmc_consumer_endpoint;
using ::crucible::safety::proto::mpmc_channel_session::mint_mpmc_producer_session;
using ::crucible::safety::proto::mpmc_channel_session::mint_mpmc_consumer_session;
}  // namespace mpmc

// ═════════════════════════════════════════════════════════════════════
// ── Calendar grid (per-cycle producer/consumer) ────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace calendar_grid {
using ::crucible::safety::proto::calendar_grid_session::mint_calendar_grid_producer;
using ::crucible::safety::proto::calendar_grid_session::mint_calendar_grid_consumer;
using ::crucible::safety::proto::calendar_grid_session::mint_producer_session;
using ::crucible::safety::proto::calendar_grid_session::mint_consumer_session;
}  // namespace calendar_grid

// ═════════════════════════════════════════════════════════════════════
// ── Sharded calendar grid ──────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace sharded_calendar_grid {
using ::crucible::safety::proto::sharded_calendar_grid_session::mint_sharded_calendar_grid_producer;
using ::crucible::safety::proto::sharded_calendar_grid_session::mint_sharded_calendar_grid_consumer;
using ::crucible::safety::proto::sharded_calendar_grid_session::mint_producer_session;
using ::crucible::safety::proto::sharded_calendar_grid_session::mint_consumer_session;
}  // namespace sharded_calendar_grid

// ═════════════════════════════════════════════════════════════════════
// ── Sharded grid ───────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace sharded_grid {
using ::crucible::safety::proto::sharded_grid_session::mint_sharded_grid_producer;
using ::crucible::safety::proto::sharded_grid_session::mint_sharded_grid_consumer;
using ::crucible::safety::proto::sharded_grid_session::mint_producer_session;
using ::crucible::safety::proto::sharded_grid_session::mint_consumer_session;
}  // namespace sharded_grid

}  // namespace crucible::fixy::substr
