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

#include <crucible/concurrent/ChaseLevDeque.h>               // FIXY-V-047: DequeValue concept
#include <crucible/concurrent/MpmcRing.h>                   // FIXY-V-046: MpmcValue concept
#include <crucible/concurrent/PermissionedChaseLevDeque.h>  // FIXY-V-047: substrate-direct work-stealing surface
#include <crucible/concurrent/PermissionedMpmcChannel.h>    // FIXY-V-046: substrate-direct MPMC surface
#include <crucible/concurrent/PermissionedMpscChannel.h>
#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/concurrent/PermissionedSpscChannel.h>  // FIXY-V-045: substrate-direct SPSC surface
#include <crucible/concurrent/SpscRing.h>                  // FIXY-V-045: SpscValue concept
#include <crucible/concurrent/Substrate.h>          // FIXY-U-051: ChannelTopology + IsSubstrate family
#include <crucible/concurrent/SubstrateCtxFit.h>    // FIXY-U-051: ctx-fit concepts + tier metafns
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

// ── FIXY-V-045: substrate-direct surface ─────────────────────────
//
// Pre-V-045 the spsc:: sub-namespace surfaced only the ctx-bound
// session mints (mint_producer_session / mint_consumer_session) —
// callers who wanted to construct a PermissionedSpscChannel, mint
// its Whole permission, or split into Producer/Consumer halves had
// to descend into <crucible/concurrent/PermissionedSpscChannel.h>
// directly.  V-045 closes the discoverability gap by re-exporting
// the substrate primitive, its permission tag tree, the SpscValue
// concept, a surface concept gating endpoint mints, and inline
// endpoint mint shims that lift ch.producer(perm) / ch.consumer(perm)
// behind a session-side mint name (mirror of mpsc::, lines below).
//
// Surface (additive, all alias-template / inline / using-decl —
// pure name-lookup, zero runtime cost).

// Substrate alias — full re-export including default UserTag = void.
template <::crucible::concurrent::SpscValue T,
          std::size_t Capacity,
          typename UserTag = void>
using PermissionedSpscChannel =
    ::crucible::concurrent::PermissionedSpscChannel<T, Capacity, UserTag>;

// SpscValue concept re-export (using-decl form — concepts can be
// brought in by name).  Bumps substr_spsc_using cardinality 2 → 3.
using ::crucible::concurrent::SpscValue;

// spsc_tag sub-namespace — Whole / Producer / Consumer permission
// tag templates.  Callers spell these out at mint_permission_root /
// mint_permission_split call sites; surfacing them under fixy::
// substr::spsc::spsc_tag::* removes the descend-into-concurrent
// requirement.
namespace spsc_tag {
template <typename UserTag>
using Whole =
    ::crucible::concurrent::spsc_tag::Whole<UserTag>;
template <typename UserTag>
using Producer =
    ::crucible::concurrent::spsc_tag::Producer<UserTag>;
template <typename UserTag>
using Consumer =
    ::crucible::concurrent::spsc_tag::Consumer<UserTag>;
}  // namespace spsc_tag

// Surface concept — gates the endpoint mints below.  Differs from
// MpscChannelSessionSurface (in mpsc:: namespace) in two ways:
//   1. ch.producer(perm) takes a Linear Permission<producer_tag>&&
//      and returns a bare ProducerHandle (NOT optional).  SPSC's
//      try_push is single-producer-only; the producer side is
//      structurally Linear, not Pool-mediated.
//   2. ch.consumer(perm) is symmetric — Linear Permission<consumer_tag>&&
//      → bare ConsumerHandle.  Both endpoints linear: the channel
//      has EXACTLY one producer and EXACTLY one consumer at any time.
template <typename Channel>
concept SpscChannelSessionSurface = requires(
    Channel& ch,
    typename Channel::ProducerHandle& producer_handle,
    typename Channel::ConsumerHandle& consumer_handle,
    ::crucible::safety::Permission<typename Channel::producer_tag>&& prod_perm,
    ::crucible::safety::Permission<typename Channel::consumer_tag>&& cons_perm,
    const typename Channel::value_type& sample_payload)
{
    typename Channel::value_type;
    typename Channel::user_tag;
    typename Channel::whole_tag;
    typename Channel::producer_tag;
    typename Channel::consumer_tag;
    typename Channel::ProducerHandle;
    typename Channel::ConsumerHandle;

    // Endpoint factories — both Linear.
    { ch.producer(std::move(prod_perm)) }
        -> std::same_as<typename Channel::ProducerHandle>;
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
// every per-substrate `<substrate>::mint_*_endpoint` follows the
// same `mint_<substrate>_<role>_endpoint(ch, perm)` shape grep-finds.
//
// sessions/SpscSession.h ships only ctx-bound session mints; these
// endpoint shims are NEW in V-045 and live INLINE in fixy/Substr.h
// (mirror of mpsc:: precedent which also defines its endpoint shims
// inline).  Future migration: when sessions/SpscSession.h grows
// canonical endpoint mints, replace these with using-decls.

template <SpscChannelSessionSurface Channel>
[[nodiscard]] constexpr auto mint_spsc_producer_endpoint(
    Channel& ch,
    ::crucible::safety::Permission<typename Channel::producer_tag>&& perm) noexcept
    -> typename Channel::ProducerHandle
{
    return ch.producer(std::move(perm));
}

template <SpscChannelSessionSurface Channel>
[[nodiscard]] constexpr auto mint_spsc_consumer_endpoint(
    Channel& ch,
    ::crucible::safety::Permission<typename Channel::consumer_tag>&& perm) noexcept
    -> typename Channel::ConsumerHandle
{
    return ch.consumer(std::move(perm));
}

// Mint factories (ctx-bound) — already shipped in sessions/SpscSession.h.
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

// ── FIXY-V-047: substrate-direct surface enrichment ──────────────
//
// Mirror of V-045's spsc:: and V-046's mpmc:: surface treatment.
// PermissionedChaseLevDeque is THE worked work-stealing example in
// the CSL/separation-logic stack: ASYMMETRIC linear × fractional
// permissions — one Linear Owner (push_bottom / pop_bottom) and many
// Fractional Thieves (steal_top via internally-minted Pool root).
// The thief pool is constructed inside the substrate (mirror of
// PermissionedSnapshot's reader pool), so callers mint only the
// linear Owner Permission externally.
//
// Pre-V-047 the chaselev:: sub-namespace surfaced only the ctx-bound
// session mints + endpoint mints — callers who wanted to construct
// a PermissionedChaseLevDeque, mint its Whole permission, or split
// into Owner/Thief halves had to descend into
// <crucible/concurrent/PermissionedChaseLevDeque.h> directly.  V-047
// closes the discoverability gap by re-exporting the substrate
// primitive, its three-tag permission tree, and the DequeValue
// concept (the same shape spsc surfaces SpscValue and mpmc surfaces
// MpmcValue).
//
// Surface (additive, all alias-template / using-decl — pure name
// lookup, zero runtime cost).  No new mint factories — endpoint and
// session mints already shipped from sessions/ChaseLevDequeSession.h.

// Substrate alias — full re-export including default UserTag = void.
// DequeValue T constraint surfaced for diagnostic clarity (same
// pattern as fixy::substr::spsc::PermissionedSpscChannel).
template <::crucible::concurrent::DequeValue T,
          std::size_t Capacity,
          typename UserTag = void>
using PermissionedChaseLevDeque =
    ::crucible::concurrent::PermissionedChaseLevDeque<T, Capacity, UserTag>;

// DequeValue concept re-export (using-decl form — concepts can be
// brought in by name).  Bumps substr_chaselev_using cardinality 4 → 5.
using ::crucible::concurrent::DequeValue;

// deque_tag sub-namespace — Whole / Owner / Thief permission tag
// templates.  Although the work-stealing CSL split is asymmetric
// (Owner is Linear, Thief is Fractional via internal pool), the
// substrate ships all three tags + a splits_into<Whole, Owner, Thief>
// specialization so callers may use the standard mint_permission_split
// rail for the Whole Permission if they prefer (the substrate's
// default-ctor mints the Thief pool root internally; an external
// Whole→Owner+Thief split is also valid for callers that want explicit
// pool construction).
namespace deque_tag {
template <typename UserTag>
using Whole =
    ::crucible::concurrent::deque_tag::Whole<UserTag>;
template <typename UserTag>
using Owner =
    ::crucible::concurrent::deque_tag::Owner<UserTag>;
template <typename UserTag>
using Thief =
    ::crucible::concurrent::deque_tag::Thief<UserTag>;
}  // namespace deque_tag

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

// ── FIXY-V-046: substrate-direct surface enrichment ──────────────
//
// Mirror of V-045's spsc:: surface treatment.  PermissionedMpmcChannel
// is THE worked MPMC example in the CSL/separation-logic stack:
// fractional Producer × fractional Consumer permissions, refcounted
// SharedPermissionPool on each side, optional<Handle> return from
// producer()/consumer() factories (multi-party contention can fail
// when the pool is exhausted).  The four additions below pull the
// substrate-side type/concept identity into fixy::substr::mpmc::
// alongside the already-shipped session mints.

// Substrate alias — PermissionedMpmcChannel<T, Cap, UserTag>.  Class
// templates surface via alias-template, not a using-declaration; this
// does not bump substr_mpmc_using.
template <typename T, std::size_t Capacity, typename UserTag = void>
using PermissionedMpmcChannel =
    ::crucible::concurrent::PermissionedMpmcChannel<T, Capacity, UserTag>;

// MpmcValue concept re-export (bumps substr_mpmc_using cardinality 4 → 5).
using ::crucible::concurrent::MpmcValue;

// Tag tree — fractional × fractional Whole/Producer/Consumer.
// mpmc_tag is a nested namespace; alias templates do not count as
// using-declarations toward substr_mpmc_using.
namespace mpmc_tag {
template <typename UserTag>
using Whole = ::crucible::concurrent::mpmc_tag::Whole<UserTag>;
template <typename UserTag>
using Producer = ::crucible::concurrent::mpmc_tag::Producer<UserTag>;
template <typename UserTag>
using Consumer = ::crucible::concurrent::mpmc_tag::Consumer<UserTag>;
}  // namespace mpmc_tag

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

// ═════════════════════════════════════════════════════════════════════
// ── FIXY-U-051: topology + ctx-fit concept surface ─────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Pre-U-051 the fixy::substr:: surface exposed only the per-substrate
// mint factories; production callsites that wanted to declare a
// `template <IsSubstrate S, IsExecCtx Ctx> requires
// SubstrateFitsCtxResidency<S, Ctx>` signature had to descend into
// `<crucible/concurrent/Substrate*.h>` directly.  The §XXI hard-gate
// concept lived one namespace removed from where every Substrate
// mint already lived — a discoverability gap that violated the
// "fixy covers the substrate tree" promise.
//
// U-051 closes the gap.  Five categories of surface:
//   1. ChannelTopology enum + Substrate<...> template family
//   2. Trait accessors (substrate_topology_v / value_type_t / etc.)
//   3. Byte-footprint metafunctions (channel_byte_footprint_v,
//      per_call_working_set_v)
//   4. IsSubstrate hierarchy (IsSubstrate + 5 topology-refined)
//   5. Tier enum + ctx-fit concepts + tier-required metafns
//
// All re-exports are pure name-lookup directives; concept gates pass
// through verbatim (C++20 `using` brings concept names just like
// type aliases).  Sentinel witnesses below pin every re-exported
// symbol's substrate identity, plus a cardinality witness at the
// tail.

// ── ChannelTopology enum + Substrate template family ───────────────
using ::crucible::concurrent::ChannelTopology;
using ::crucible::concurrent::Substrate;
using ::crucible::concurrent::Substrate_t;

// ── Trait accessors ────────────────────────────────────────────────
using ::crucible::concurrent::substrate_traits;
using ::crucible::concurrent::is_substrate;
using ::crucible::concurrent::is_substrate_v;
using ::crucible::concurrent::substrate_topology_v;
using ::crucible::concurrent::substrate_value_type_t;
using ::crucible::concurrent::substrate_user_tag_t;
using ::crucible::concurrent::substrate_capacity_v;

// ── Byte-footprint metafunctions ───────────────────────────────────
using ::crucible::concurrent::channel_byte_footprint_v;
using ::crucible::concurrent::per_call_working_set_v;

// ── Topology recommendation helpers ────────────────────────────────
using ::crucible::concurrent::recommend_topology;
using ::crucible::concurrent::recommend_topology_for_workload;
using ::crucible::concurrent::conservative_cliff_l2_per_core;

// ── IsSubstrate hierarchy (concept gates) ──────────────────────────
using ::crucible::concurrent::IsSubstrate;
using ::crucible::concurrent::IsOneToOneSubstrate;
using ::crucible::concurrent::IsManyToOneSubstrate;
using ::crucible::concurrent::IsOneToManyLatestSubstrate;
using ::crucible::concurrent::IsManyToManySubstrate;
using ::crucible::concurrent::IsWorkStealingSubstrate;

// ── Tier enum + cache-tier constants ───────────────────────────────
using ::crucible::concurrent::Tier;
using ::crucible::concurrent::fits_in_tier_v;
using ::crucible::concurrent::required_tier_for_footprint;
using ::crucible::concurrent::substrate_required_tier_v;
using ::crucible::concurrent::substrate_hot_path_required_tier_v;
using ::crucible::concurrent::conservative_l1d_per_core;
using ::crucible::concurrent::conservative_l2_per_core;
using ::crucible::concurrent::conservative_l3_total;

// ── Ctx-fit concept gates (the §XXI hard-gate trinity) ─────────────
using ::crucible::concurrent::SubstrateFitsCtxResidency;
using ::crucible::concurrent::StorageFitsCtxResidency;
using ::crucible::concurrent::SubstrateBenefitsFromParallelism;

}  // namespace crucible::fixy::substr

// ─── FIXY-U-103 in-header sentinel ─────────────────────────────────
//
// Drift-catch for the per-substrate sub-namespaces: spsc (3), swmr (6),
// chaselev (5), metalog (4), chainedge (4), mpmc (5), calendar_grid (4),
// sharded_calendar_grid (4), sharded_grid (4), snapshot (4), and the
// outer-level mint_substrate_session re-export (1).  mpsc:: is NOT a
// pure re-export — it defines its own mint factories (also covered by
// dedicated test_fixy_substr_mpsc fixtures) so it doesn't contribute
// to the using-decl cardinality.  spsc::SpscValue using-decl added by
// FIXY-V-045 (substrate-direct surface enrichment) bumped spsc 2 → 3.
// mpmc::MpmcValue using-decl added by FIXY-V-046 bumped mpmc 4 → 5
// (substrate alias template + tag tree alias templates do NOT count
// — only the MpmcValue using-decl bumps the cardinality witness).
// chaselev::DequeValue using-decl added by FIXY-V-047 bumped chaselev
// 4 → 5 (same pattern as V-045 / V-046 — substrate alias template +
// deque_tag tree alias templates do NOT count toward the cardinality
// witness; only the concept using-decl does).
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

// ── FIXY-V-045: SPSC substrate-direct surface witnesses ──────────
//
// Pin every V-045 addition to the substrate.  Identity for class
// templates + tag templates via std::is_same_v; concept admission
// for SpscChannelSessionSurface against a representative channel
// instantiation; bare-presence witness for endpoint mint shims via
// decltype on a synthetic call expression (NOT invoked — would
// require a real channel instance).

namespace v045 {

struct V045ProbeUserTag {};

// 1. Substrate alias identity — fixy path === concurrent path.
using SpscViaFixy = ::crucible::fixy::substr::spsc::PermissionedSpscChannel<
    int, 64, V045ProbeUserTag>;
using SpscViaConcurrent = ::crucible::concurrent::PermissionedSpscChannel<
    int, 64, V045ProbeUserTag>;
static_assert(std::is_same_v<SpscViaFixy, SpscViaConcurrent>,
    "fixy::substr::spsc::PermissionedSpscChannel must alias the substrate.");

// 2. SpscValue concept admission parity (using-decl preserves
// concept semantics).
static_assert(::crucible::fixy::substr::spsc::SpscValue<int> ==
              ::crucible::concurrent::SpscValue<int>);
static_assert(::crucible::fixy::substr::spsc::SpscValue<int>);

// 3. Tag template identity — Whole/Producer/Consumer alias the
// substrate's spsc_tag tree exactly.
static_assert(std::is_same_v<
    ::crucible::fixy::substr::spsc::spsc_tag::Whole<V045ProbeUserTag>,
    ::crucible::concurrent::spsc_tag::Whole<V045ProbeUserTag>>);
static_assert(std::is_same_v<
    ::crucible::fixy::substr::spsc::spsc_tag::Producer<V045ProbeUserTag>,
    ::crucible::concurrent::spsc_tag::Producer<V045ProbeUserTag>>);
static_assert(std::is_same_v<
    ::crucible::fixy::substr::spsc::spsc_tag::Consumer<V045ProbeUserTag>,
    ::crucible::concurrent::spsc_tag::Consumer<V045ProbeUserTag>>);

// 4. Tag identity propagates through SpscViaFixy's member typedefs.
static_assert(std::is_same_v<
    typename SpscViaFixy::whole_tag,
    ::crucible::fixy::substr::spsc::spsc_tag::Whole<V045ProbeUserTag>>);
static_assert(std::is_same_v<
    typename SpscViaFixy::producer_tag,
    ::crucible::fixy::substr::spsc::spsc_tag::Producer<V045ProbeUserTag>>);
static_assert(std::is_same_v<
    typename SpscViaFixy::consumer_tag,
    ::crucible::fixy::substr::spsc::spsc_tag::Consumer<V045ProbeUserTag>>);

// 5. SpscChannelSessionSurface admits the representative channel.
static_assert(
    ::crucible::fixy::substr::spsc::SpscChannelSessionSurface<SpscViaFixy>);

// 6. channel_capacity passes through (value-template parity).
static_assert(SpscViaFixy::channel_capacity == 64);
static_assert(SpscViaFixy::channel_capacity ==
              SpscViaConcurrent::channel_capacity);

// 7. value_type identity — substrate's T == fixy's T.
static_assert(std::is_same_v<typename SpscViaFixy::value_type, int>);

}  // namespace v045

// ── FIXY-V-046: MPMC substrate-direct surface witnesses ──────────
//
// Same recipe as v045:: above.  Pin every V-046 addition: substrate
// alias identity, MpmcValue concept admission parity, tag tree
// identity, member-typedef propagation, channel_capacity passthrough,
// value_type identity, surface concept admission.  MPMC differs from
// SPSC in fractional × fractional permission shape (refcounted Pool
// on both sides), but the surface witnesses are structurally
// symmetric.

namespace v046 {

struct V046ProbeUserTag {};

// 1. Substrate alias identity — fixy path === concurrent path.
using MpmcViaFixy = ::crucible::fixy::substr::mpmc::PermissionedMpmcChannel<
    int, 64, V046ProbeUserTag>;
using MpmcViaConcurrent = ::crucible::concurrent::PermissionedMpmcChannel<
    int, 64, V046ProbeUserTag>;
static_assert(std::is_same_v<MpmcViaFixy, MpmcViaConcurrent>,
    "fixy::substr::mpmc::PermissionedMpmcChannel must alias the substrate.");

// 2. MpmcValue concept admission parity (using-decl preserves
// concept semantics).
static_assert(::crucible::fixy::substr::mpmc::MpmcValue<int> ==
              ::crucible::concurrent::MpmcValue<int>);
static_assert(::crucible::fixy::substr::mpmc::MpmcValue<int>);

// 3. Tag template identity — Whole/Producer/Consumer alias the
// substrate's mpmc_tag tree exactly.
static_assert(std::is_same_v<
    ::crucible::fixy::substr::mpmc::mpmc_tag::Whole<V046ProbeUserTag>,
    ::crucible::concurrent::mpmc_tag::Whole<V046ProbeUserTag>>);
static_assert(std::is_same_v<
    ::crucible::fixy::substr::mpmc::mpmc_tag::Producer<V046ProbeUserTag>,
    ::crucible::concurrent::mpmc_tag::Producer<V046ProbeUserTag>>);
static_assert(std::is_same_v<
    ::crucible::fixy::substr::mpmc::mpmc_tag::Consumer<V046ProbeUserTag>,
    ::crucible::concurrent::mpmc_tag::Consumer<V046ProbeUserTag>>);

// 4. Tag identity propagates through MpmcViaFixy's member typedefs.
static_assert(std::is_same_v<
    typename MpmcViaFixy::whole_tag,
    ::crucible::fixy::substr::mpmc::mpmc_tag::Whole<V046ProbeUserTag>>);
static_assert(std::is_same_v<
    typename MpmcViaFixy::producer_tag,
    ::crucible::fixy::substr::mpmc::mpmc_tag::Producer<V046ProbeUserTag>>);
static_assert(std::is_same_v<
    typename MpmcViaFixy::consumer_tag,
    ::crucible::fixy::substr::mpmc::mpmc_tag::Consumer<V046ProbeUserTag>>);

// 5. MpmcChannelSessionSurface admits the representative channel.
static_assert(
    ::crucible::fixy::substr::mpmc::MpmcChannelSessionSurface<MpmcViaFixy>);

// 6. channel_capacity passes through (value-template parity).
static_assert(MpmcViaFixy::channel_capacity == 64);
static_assert(MpmcViaFixy::channel_capacity ==
              MpmcViaConcurrent::channel_capacity);

// 7. value_type identity — substrate's T == fixy's T.
static_assert(std::is_same_v<typename MpmcViaFixy::value_type, int>);

}  // namespace v046

// ── FIXY-V-047: Chase-Lev substrate-direct surface witnesses ─────
//
// Same recipe as v045:: / v046:: above.  Pin every V-047 addition:
// substrate alias identity, DequeValue concept admission parity,
// tag tree identity (three tags — Whole / Owner / Thief, asymmetric
// linear × fractional CSL split), member-typedef propagation,
// deque_capacity passthrough, value_type identity, surface concept
// admission.  ChaseLev differs structurally from SPSC/MPMC in that
// Owner is Linear and Thief is Fractional via internal pool, but
// the substrate-direct surface witnesses are structurally identical.

namespace v047 {

struct V047ProbeUserTag {};

// 1. Substrate alias identity — fixy path === concurrent path.
using DequeViaFixy = ::crucible::fixy::substr::chaselev::PermissionedChaseLevDeque<
    int, 64, V047ProbeUserTag>;
using DequeViaConcurrent = ::crucible::concurrent::PermissionedChaseLevDeque<
    int, 64, V047ProbeUserTag>;
static_assert(std::is_same_v<DequeViaFixy, DequeViaConcurrent>,
    "fixy::substr::chaselev::PermissionedChaseLevDeque must alias the substrate.");

// 2. DequeValue concept admission parity (using-decl preserves
// concept semantics).
static_assert(::crucible::fixy::substr::chaselev::DequeValue<int> ==
              ::crucible::concurrent::DequeValue<int>);
static_assert(::crucible::fixy::substr::chaselev::DequeValue<int>);

// 3. Tag template identity — Whole / Owner / Thief alias the
// substrate's deque_tag tree exactly.
static_assert(std::is_same_v<
    ::crucible::fixy::substr::chaselev::deque_tag::Whole<V047ProbeUserTag>,
    ::crucible::concurrent::deque_tag::Whole<V047ProbeUserTag>>);
static_assert(std::is_same_v<
    ::crucible::fixy::substr::chaselev::deque_tag::Owner<V047ProbeUserTag>,
    ::crucible::concurrent::deque_tag::Owner<V047ProbeUserTag>>);
static_assert(std::is_same_v<
    ::crucible::fixy::substr::chaselev::deque_tag::Thief<V047ProbeUserTag>,
    ::crucible::concurrent::deque_tag::Thief<V047ProbeUserTag>>);

// 4. Tag identity propagates through DequeViaFixy's member typedefs.
// Note: no producer_tag/consumer_tag on chaselev — Owner is the
// privileged Linear side, Thief is the Fractional side.
static_assert(std::is_same_v<
    typename DequeViaFixy::whole_tag,
    ::crucible::fixy::substr::chaselev::deque_tag::Whole<V047ProbeUserTag>>);
static_assert(std::is_same_v<
    typename DequeViaFixy::owner_tag,
    ::crucible::fixy::substr::chaselev::deque_tag::Owner<V047ProbeUserTag>>);
static_assert(std::is_same_v<
    typename DequeViaFixy::thief_tag,
    ::crucible::fixy::substr::chaselev::deque_tag::Thief<V047ProbeUserTag>>);

// 5. ChaseLevSessionSurface admits the representative deque.
static_assert(
    ::crucible::fixy::substr::chaselev::ChaseLevSessionSurface<DequeViaFixy>);

// 6. deque_capacity passes through (value-template parity).
static_assert(DequeViaFixy::deque_capacity == 64);
static_assert(DequeViaFixy::deque_capacity ==
              DequeViaConcurrent::deque_capacity);

// 7. value_type identity — substrate's T == fixy's T.
static_assert(std::is_same_v<typename DequeViaFixy::value_type, int>);

}  // namespace v047

// ── Per-sub-namespace cardinality witnesses ──────────────────────

constexpr int substr_spsc_using                  = 3;
constexpr int substr_swmr_using                  = 6;
constexpr int substr_chaselev_using              = 5;
constexpr int substr_metalog_using               = 4;
constexpr int substr_chainedge_using             = 4;
constexpr int substr_mpmc_using                  = 5;
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

static_assert(substr_total_using == 44,
    "fixy::substr:: using-decl surface drifted from 44 — Substr.h "
    "sub-namespace re-exports and this sentinel must update in lockstep.");

// ── FIXY-U-051: topology + ctx-fit concept-surface witnesses ───────
//
// The U-051 surface re-exports the §XXI hard-gate trinity
// (SubstrateFitsCtxResidency / StorageFitsCtxResidency /
// SubstrateBenefitsFromParallelism) plus the topology family + tier
// metafunctions.  Concepts have no type so identity is proven by
// behavioral equivalence on canonical Substrate × ExecCtx pairings —
// the fixy::substr:: path MUST agree bit-for-bit with the substrate's
// ::crucible::concurrent:: gate on every input the substrate's own
// self-test exercises.
//
// Type-identity for non-concept items (enums, traits, value templates)
// is asserted directly via std::is_same_v / equality.

namespace u051 {

struct U051ProbeUserTag {};

namespace cc  = ::crucible::concurrent;
namespace eff = ::crucible::effects;

// ── 1. ChannelTopology enum value identity ─────────────────────────
static_assert(static_cast<int>(ChannelTopology::OneToOne) ==
              static_cast<int>(cc::ChannelTopology::OneToOne));
static_assert(static_cast<int>(ChannelTopology::WorkStealing) ==
              static_cast<int>(cc::ChannelTopology::WorkStealing));

// ── 2. Substrate_t alias identity (template re-export round-trips) ─
using SpscViaFixy = Substrate_t<ChannelTopology::OneToOne, int, 1024,
                                 U051ProbeUserTag>;
using SpscViaSubstrate = cc::Substrate_t<cc::ChannelTopology::OneToOne, int,
                                          1024, U051ProbeUserTag>;
static_assert(std::is_same_v<SpscViaFixy, SpscViaSubstrate>,
    "fixy::substr::Substrate_t must alias concurrent::Substrate_t");

using SnapViaFixy = Substrate_t<ChannelTopology::OneToMany_Latest, double, 0,
                                 U051ProbeUserTag>;
using SnapViaSubstrate = cc::Substrate_t<cc::ChannelTopology::OneToMany_Latest,
                                          double, 0, U051ProbeUserTag>;
static_assert(std::is_same_v<SnapViaFixy, SnapViaSubstrate>);

// ── 3. Trait accessor parity ───────────────────────────────────────
static_assert(substrate_topology_v<SpscViaFixy> ==
              cc::substrate_topology_v<SpscViaSubstrate>);
static_assert(substrate_capacity_v<SpscViaFixy> ==
              cc::substrate_capacity_v<SpscViaSubstrate>);
static_assert(std::is_same_v<substrate_value_type_t<SpscViaFixy>,
                             cc::substrate_value_type_t<SpscViaSubstrate>>);
static_assert(std::is_same_v<substrate_user_tag_t<SpscViaFixy>,
                             cc::substrate_user_tag_t<SpscViaSubstrate>>);

// ── 4. Byte-footprint metafunctions agree ──────────────────────────
static_assert(channel_byte_footprint_v<SpscViaFixy> ==
              cc::channel_byte_footprint_v<SpscViaSubstrate>);
static_assert(per_call_working_set_v<SpscViaFixy> ==
              cc::per_call_working_set_v<SpscViaSubstrate>);

// ── 5. IsSubstrate hierarchy (concept gates) ───────────────────────
static_assert(IsSubstrate<SpscViaFixy>);
static_assert(IsSubstrate<SnapViaFixy>);
static_assert(IsOneToOneSubstrate<SpscViaFixy>);
static_assert(!IsOneToOneSubstrate<SnapViaFixy>);
static_assert(IsOneToManyLatestSubstrate<SnapViaFixy>);
static_assert(!IsManyToManySubstrate<SpscViaFixy>);
static_assert(!IsWorkStealingSubstrate<SpscViaFixy>);
static_assert(!IsManyToOneSubstrate<SpscViaFixy>);

// Concepts agree with substrate paths on the same input.
static_assert(IsSubstrate<SpscViaFixy> ==
              cc::IsSubstrate<SpscViaSubstrate>);
static_assert(IsOneToOneSubstrate<SpscViaFixy> ==
              cc::IsOneToOneSubstrate<SpscViaSubstrate>);

// ── 6. Tier enum + tier metafunctions ──────────────────────────────
static_assert(static_cast<int>(Tier::L1Resident) ==
              static_cast<int>(cc::Tier::L1Resident));
static_assert(static_cast<int>(Tier::DRAMBound) ==
              static_cast<int>(cc::Tier::DRAMBound));

// Cache-tier constants pass through.
static_assert(conservative_l1d_per_core == cc::conservative_l1d_per_core);
static_assert(conservative_l2_per_core  == cc::conservative_l2_per_core);
static_assert(conservative_l3_total     == cc::conservative_l3_total);
static_assert(conservative_cliff_l2_per_core ==
              cc::conservative_cliff_l2_per_core);

// fits_in_tier_v parity on a representative pair.
static_assert(fits_in_tier_v<4 * 1024, Tier::L1Resident> ==
              cc::fits_in_tier_v<4 * 1024, cc::Tier::L1Resident>);
static_assert(fits_in_tier_v<4 * 1024, Tier::L1Resident>);
static_assert(!fits_in_tier_v<256 * 1024, Tier::L1Resident>);

// required_tier_for_footprint inverse mapping passes through.
static_assert(required_tier_for_footprint<sizeof(double)> ==
              cc::required_tier_for_footprint<sizeof(double)>);
static_assert(required_tier_for_footprint<sizeof(double)> == Tier::L1Resident);

// substrate_required_tier_v + hot-path variant.
static_assert(substrate_required_tier_v<SpscViaFixy> ==
              cc::substrate_required_tier_v<SpscViaSubstrate>);
static_assert(substrate_hot_path_required_tier_v<SpscViaFixy> ==
              cc::substrate_hot_path_required_tier_v<SpscViaSubstrate>);
static_assert(substrate_hot_path_required_tier_v<SpscViaFixy> ==
              Tier::L1Resident);

// ── 7. Ctx-fit concept trinity — behavioral equivalence ────────────
//
// HotFgCtx is L1-resident.  A small SPSC's per-call WS fits L1, so
// SubstrateFitsCtxResidency must be true through BOTH paths.
static_assert(SubstrateFitsCtxResidency<SpscViaFixy, eff::HotFgCtx> ==
              cc::SubstrateFitsCtxResidency<SpscViaSubstrate, eff::HotFgCtx>);
static_assert(SubstrateFitsCtxResidency<SpscViaFixy, eff::HotFgCtx>);
static_assert(SubstrateFitsCtxResidency<SpscViaFixy, eff::BgDrainCtx>);
static_assert(SubstrateFitsCtxResidency<SpscViaFixy, eff::ColdInitCtx>);

// StorageFitsCtxResidency: 4 KB SPSC fits L1 in storage too.
static_assert(StorageFitsCtxResidency<SpscViaFixy, eff::HotFgCtx> ==
              cc::StorageFitsCtxResidency<SpscViaSubstrate, eff::HotFgCtx>);
static_assert(StorageFitsCtxResidency<SpscViaFixy, eff::HotFgCtx>);

// SubstrateBenefitsFromParallelism: small SPSC is below the cliff.
static_assert(SubstrateBenefitsFromParallelism<SpscViaFixy> ==
              cc::SubstrateBenefitsFromParallelism<SpscViaSubstrate>);
static_assert(!SubstrateBenefitsFromParallelism<SpscViaFixy>);

// Large SPSC (4 MB) crosses the cliff → BenefitsFromParallelism true.
using LargeSpsc = Substrate_t<ChannelTopology::OneToOne, int,
                               1024 * 1024, U051ProbeUserTag>;
static_assert(SubstrateBenefitsFromParallelism<LargeSpsc>);
// But its per-call WS still fits L1 (counters + 1 cell << 32 KB).
static_assert(SubstrateFitsCtxResidency<LargeSpsc, eff::HotFgCtx>);

// ── 8. recommend_topology helper ───────────────────────────────────
static_assert(recommend_topology(1, 1) == ChannelTopology::OneToOne);
static_assert(recommend_topology(4, 1) == ChannelTopology::ManyToOne);
static_assert(recommend_topology(4, 4) == ChannelTopology::ManyToMany);
static_assert(recommend_topology(1, 4, /*latest_only=*/true) ==
              ChannelTopology::OneToMany_Latest);
static_assert(recommend_topology_for_workload(1, 1, 4 * 1024) ==
              ChannelTopology::OneToOne);
static_assert(recommend_topology_for_workload(4, 4, 4 * 1024 * 1024) ==
              ChannelTopology::ManyToMany);

// ── 9. Cardinality witness ─────────────────────────────────────────
//
// 32 using-decls added for FIXY-U-051 (excluding mint_substrate_session
// which was already there pre-U-051).  A future drift forces this
// constant + the using-decl block to update in lockstep.  Prose count
// refreshed by FIXY-U-132 to match constant + breakdown sum below
// (Class G drift — comment count drifting from constant).
//
// Breakdown:
//   ChannelTopology / Substrate / Substrate_t                        3
//   substrate_traits / is_substrate / is_substrate_v                 3
//   substrate_topology_v / value_type_t / user_tag_t / capacity_v    4
//   channel_byte_footprint_v / per_call_working_set_v                2
//   recommend_topology / recommend_topology_for_workload /
//     conservative_cliff_l2_per_core                                 3
//   IsSubstrate / IsOneToOne / IsManyToOne / IsOneToManyLatest /
//     IsManyToMany / IsWorkStealing                                  6
//   Tier / fits_in_tier_v / required_tier_for_footprint /
//     substrate_required_tier_v / substrate_hot_path_required_tier_v 5
//   conservative_l1d_per_core / conservative_l2_per_core /
//     conservative_l3_total                                          3
//   SubstrateFitsCtxResidency / StorageFitsCtxResidency /
//     SubstrateBenefitsFromParallelism                               3
//                                                                  ───
//                                                                   32
constexpr int u051_surface_cardinality = 32;
static_assert(u051_surface_cardinality == 32,
    "FIXY-U-051 surface (topology + ctx-fit) drifted from 32 — Substr.h "
    "U-051 block and this sentinel must update in lockstep.");

}  // namespace u051

}  // namespace crucible::fixy::substr::self_test
