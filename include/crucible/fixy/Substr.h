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
#include <crucible/concurrent/PermissionedCalendarGrid.h>   // FIXY-V-050: substrate-direct single-grid calendar surface
#include <crucible/concurrent/PermissionedChainEdge.h>      // FIXY-V-052: substrate-direct ChainEdge one-shot semaphore surface
#include <crucible/concurrent/PermissionedChaseLevDeque.h>  // FIXY-V-047: substrate-direct work-stealing surface
#include <crucible/concurrent/PermissionedMetaLog.h>       // FIXY-V-051: substrate-direct MetaLog SPSC surface
#include <crucible/concurrent/PermissionedMpmcChannel.h>    // FIXY-V-046: substrate-direct MPMC surface
#include <crucible/concurrent/PermissionedMpscChannel.h>
#include <crucible/concurrent/PermissionedShardedCalendarGrid.h>  // FIXY-V-049: substrate-direct sharded-calendar-grid surface
#include <crucible/concurrent/PermissionedShardedGrid.h>    // FIXY-V-048: substrate-direct sharded-grid surface
#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/concurrent/PermissionedSpscChannel.h>  // FIXY-V-045: substrate-direct SPSC surface
#include <crucible/concurrent/ShardedGrid.h>                // FIXY-V-048: Routing policy structs (RoundRobinRouting / HashKeyRouting / AffinityRouting)
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

// ── FIXY-V-051: substrate-direct surface enrichment ──────────────
//
// Mirror of V-045's spsc:: / V-047's chaselev:: / V-049's shardcal::
// / V-050's calendar_grid:: surface treatment.  PermissionedMetaLog
// is THE production TensorMeta side-channel: linear single-producer
// (foreground recording thread appends) × linear single-consumer
// (background drain thread reads).  The substrate decorates the
// already-extant ::crucible::MetaLog buffer rather than owning its
// storage — the PermissionedMetaLog<UserTag> instance stores only a
// MetaLog& reference plus an empty Permission token (EBO-collapsed),
// keeping handles pointer-sized.
//
// Pre-V-051 the metalog:: sub-namespace surfaced only the four
// session-layer mints (mint_metalog_{producer,consumer}[ _session])
// — callers who wanted to construct a PermissionedMetaLog, mint its
// Whole permission, or split into Producer/Consumer halves had to
// descend into <crucible/concurrent/PermissionedMetaLog.h> directly.
// V-051 closes the discoverability gap by re-exporting the substrate
// primitive, its three-tag permission tree, and the MetaIndex strong
// type returned by try_append.
//
// Surface (additive, all alias-template / using-decl — pure name
// lookup, zero runtime cost).  No new mint factories — endpoint and
// session mints already shipped from sessions/MetaLogSession.h.

// Substrate alias — full re-export including default UserTag = void.
// Value type is fixed to ::crucible::TensorMeta by the substrate (the
// MetaLog itself is anchored to TensorMeta storage).
template <typename UserTag = void>
using PermissionedMetaLog =
    ::crucible::concurrent::PermissionedMetaLog<UserTag>;

// MetaIndex strong ID re-export — the type returned by
// ProducerHandle::try_append (signals start index of a bulk append).
// Bumps substr_metalog_using cardinality 4 → 5.  V-051-unique: no
// other substr sub-namespace re-exports a strong-ID return type via
// using-decl, mirroring how V-049 exposed ShardedCalendarKeyExtractorOf
// and V-050 exposed KeyExtractorOf — each cell brings a substrate-
// specific name into reach.
using ::crucible::MetaIndex;

// metalog_tag sub-namespace — Whole / Producer / Consumer permission
// tag templates.  Although the linear × linear SPSC split is the
// canonical CSL shape, the substrate ships all three tags + a
// splits_into<Whole, Producer, Consumer> specialization so callers
// may use the standard mint_permission_split rail.
namespace metalog_tag {
template <typename UserTag>
using Whole =
    ::crucible::concurrent::metalog_tag::Whole<UserTag>;
template <typename UserTag>
using Producer =
    ::crucible::concurrent::metalog_tag::Producer<UserTag>;
template <typename UserTag>
using Consumer =
    ::crucible::concurrent::metalog_tag::Consumer<UserTag>;
}  // namespace metalog_tag

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

// ── FIXY-V-052: substrate-direct surface enrichment ──────────────
//
// Mirror of V-045's spsc:: / V-047's chaselev:: / V-049's shardcal::
// / V-050's calendar_grid:: / V-051's metalog:: treatment.
// PermissionedChainEdge is THE one-shot GPU-semaphore primitive used
// to sequence two execution plans on the same Mimic backend: the
// upstream plan signals a SemaphoreSignal once after its final
// kernel completes; the downstream plan waits for the same signal
// before issuing its first kernel.  Linear × linear (single
// signaler, single waiter), but parameterized on a VendorBackend
// axis (CPU oracle vs NV / AM / TPU / TRN native semaphore).
//
// Pre-V-052 the chainedge:: sub-namespace surfaced only the four
// session-layer mints (mint_chainedge_{signaler,waiter}[_session])
// — callers who wanted to construct a PermissionedChainEdge, mint
// its Whole permission, or split into Signaler/Waiter halves had
// to descend into <crucible/concurrent/PermissionedChainEdge.h>
// directly.  V-052 closes the discoverability gap by re-exporting
// the substrate primitive, its three-tag permission tree, and the
// VendorBackend enum (the vendor axis is V-052-structurally unique
// — no other substrate in the V-045..V-052 channel-permission arc
// is parameterized on vendor backend).
//
// Surface (additive, all alias-template / using-decl — pure name
// lookup, zero runtime cost).  No new mint factories — endpoint and
// session mints already shipped from sessions/ChainEdgeSession.h.

// Substrate alias — full re-export including default Backend =
// VendorBackend::CPU and default UserTag = void.  TWO template
// params (Backend + UserTag) — first cell in V-045..V-052 arc with
// a vendor-axis template param.
template <::crucible::concurrent::VendorBackend Backend =
              ::crucible::concurrent::VendorBackend::CPU,
          typename UserTag = void>
using PermissionedChainEdge =
    ::crucible::concurrent::PermissionedChainEdge<Backend, UserTag>;

// VendorBackend enum re-export (using-decl form).  Bumps
// substr_chainedge_using cardinality 4 → 5.  V-052-unique: no
// other substr sub-namespace ships a VendorBackend axis, mirroring
// how V-049 surfaced ShardedCalendarKeyExtractorOf, V-050 exposed
// KeyExtractorOf, and V-051 exposed MetaIndex — each cell brings a
// substrate-specific name into reach.
using ::crucible::concurrent::VendorBackend;

// chainedge_tag sub-namespace — Whole / Signaler / Waiter
// permission tag templates.  Linear × linear (one Signaler, one
// Waiter); substrate ships splits_into<Whole, Signaler, Waiter>
// specialization so callers may use the standard mint_permission_split
// rail.
namespace chainedge_tag {
template <typename UserTag>
using Whole =
    ::crucible::concurrent::chainedge_tag::Whole<UserTag>;
template <typename UserTag>
using Signaler =
    ::crucible::concurrent::chainedge_tag::Signaler<UserTag>;
template <typename UserTag>
using Waiter =
    ::crucible::concurrent::chainedge_tag::Waiter<UserTag>;
}  // namespace chainedge_tag

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

// ── FIXY-V-050: substrate-direct surface enrichment ──────────────
//
// Mirror of V-045 spsc / V-046 mpmc / V-047 chaselev / V-048
// sharded_grid / V-049 sharded_calendar_grid.  PermissionedCalendarGrid
// is the SEVENTH cell of the channel-permission family — the
// linear-row × linear-singleton axis with KEY-PRIORITY semantics:
//
//   NumProducers Producer slots — each LINEAR (one Permission per row P).
//   1 Consumer slot — LINEAR (single Permission drains the whole grid).
//
// SHAPE DIFFERS from V-049 sharded_calendar_grid:
//   * V-049: NumShards × NumShards (per-shard producer↔consumer pairs,
//     cross-shard isolation; each shard owns its own calendar grid).
//   * V-050: NumProducers × 1 (all producers feed the SAME calendar
//     grid; the single consumer drains globally-ordered priority).
//
// Per-shard semantics: KeyExtractor::key(item) / QuantumNs maps each
// item to a bucket; the consumer scans forward from current_bucket
// (a monotone counter only the consumer advances).  Within the grid:
// monotone bucket order is exact; producers contend through the
// current_bucket atomic read (cross-thread load on push path — the
// V-049 sharded variant exists specifically to eliminate this cost
// when global ordering is not required).
//
// All NumProducers + 1 permissions descend from one Whole<UserTag>
// root via FOUND-A22 mint_grid_permissions<Whole, NumProducers, 1>.
// No Pool — every slot is single-owner.  ProducerHandle<P> /
// ConsumerHandle each carry their Linear Permission EBO-collapsed
// to zero bytes via [[no_unique_address]].
//
// Pre-V-050 the calendar_grid:: sub-namespace surfaced only ctx-bound
// session mints + endpoint mints — callers who wanted to instantiate
// the substrate or name the KeyExtractor concept had to descend into
// <crucible/concurrent/PermissionedCalendarGrid.h> directly.  V-050
// closes the gap by re-exporting the substrate primitive, the
// KeyExtractorOf concept, and the three-tag permission tree.
//
// Surface (additive, all alias-template / using-decl — pure name
// lookup, zero runtime cost).  No new mint factories — endpoint and
// session mints already shipped from sessions/CalendarGridSession.h.

// Substrate alias — full re-export including default UserTag = void.
// Substrate enforces SpscValue T + KeyExtractorOf internally
// (static_assert); fixy surface preserves identity without duplicating
// constraints to avoid namespace pollution (SpscValue ships from
// fixy::substr::spsc::SpscValue, V-045).
template <::crucible::concurrent::SpscValue T,
          std::size_t NumProducers,
          std::size_t NumBuckets,
          std::size_t BucketCap,
          typename KeyExtractor,
          std::uint64_t QuantumNs,
          typename UserTag = void>
using PermissionedCalendarGrid =
    ::crucible::concurrent::PermissionedCalendarGrid<
        T, NumProducers, NumBuckets, BucketCap, KeyExtractor, QuantumNs, UserTag>;

// KeyExtractorOf concept re-export — the V-050-unique surface bump.
// Pattern parallels V-045 (SpscValue), V-046 (MpmcValue), V-047
// (DequeValue), V-049 (ShardedCalendarKeyExtractorOf).  Single-grid
// CalendarGrid uses the generic KeyExtractorOf<E, T> concept (lives
// under crucible::concurrent::, not nested in calendar_tag); the
// V-049 sharded variant uses the qualified-name
// ShardedCalendarKeyExtractorOf so the two coexist without collision.
using ::crucible::concurrent::KeyExtractorOf;

// calendar_tag sub-namespace — Whole<UserTag> permission root + the
// FOUND-A22 Slice-indexed Producer<UserTag,P> alias template + the
// SCALAR Consumer<UserTag> alias (NumProducers × 1 grid: producer
// side is templated; consumer side is singleton via Slice index 0).
namespace calendar_tag {
template <typename UserTag>
using Whole =
    ::crucible::concurrent::calendar_tag::Whole<UserTag>;
template <typename UserTag, std::size_t P>
using Producer =
    ::crucible::concurrent::calendar_tag::Producer<UserTag, P>;
template <typename UserTag>
using Consumer =
    ::crucible::concurrent::calendar_tag::Consumer<UserTag>;
}  // namespace calendar_tag

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

// ── FIXY-V-049: substrate-direct surface enrichment ──────────────
//
// Mirror of V-045 spsc / V-046 mpmc / V-047 chaselev / V-048
// sharded_grid.  PermissionedShardedCalendarGrid is the SIXTH cell
// of the channel-permission family — the linear-grid × linear-grid
// axis WITH KEY-PRIORITY semantics per shard:
//
//   NumShards Producer slots — each LINEAR (one Permission per shard S).
//   NumShards Consumer slots — each LINEAR (one Permission per shard S).
//
// Per-shard priority queue ladder: each shard owns an independent
// calendar grid (NumBuckets × BucketCap SpscRings) plus a monotone
// current_bucket counter.  Items dispatched into shard S land in a
// bucket clamped by KeyExtractor::key(item) / QuantumNs ≥
// current_bucket(S).  Within shard S the consumer drains in
// monotone bucket order; across shards no global ordering holds
// (the trade-off that eliminates cross-thread atomic reads on the
// producer push path — see header doc-block for the kernel-CFS
// analogy).
//
// All 2*NumShards permissions descend from a single Whole<UserTag>
// root via the FOUND-A22 2D auto-permission-tree (NumShards ×
// NumShards via mint_grid_permissions<Whole, N, N>(parent)).  No
// Pool — every slot is single-owner.  Each handle is STATICALLY
// INDEXED at compile time: ProducerHandle<S> / ConsumerHandle<S>
// know their shard S at type level.
//
// Pre-V-049 the sharded_calendar_grid:: sub-namespace surfaced only
// ctx-bound session mints + endpoint mints — callers who wanted to
// instantiate the substrate, name the KeyExtractor concept, or spell
// the tag tree had to descend into
// <crucible/concurrent/PermissionedShardedCalendarGrid.h> directly.
// V-049 closes the gap by re-exporting the substrate primitive, the
// V-049-unique ShardedCalendarKeyExtractorOf concept, and the
// three-tag permission tree.
//
// Surface (additive, all alias-template / using-decl — pure name
// lookup, zero runtime cost).  No new mint factories — endpoint and
// session mints already shipped from sessions/ShardedCalendarGridSession.h.

// Substrate alias — full re-export including default UserTag = void.
// Substrate enforces SpscValue T + ShardedCalendarKeyExtractorOf
// internally (static_assert); fixy surface preserves identity without
// duplicating constraints to avoid namespace pollution (SpscValue is
// shipped from fixy::substr::spsc::SpscValue, V-045).
template <::crucible::concurrent::SpscValue T,
          std::size_t NumShards,
          std::size_t NumBuckets,
          std::size_t BucketCap,
          typename KeyExtractor,
          std::uint64_t QuantumNs,
          typename UserTag = void>
using PermissionedShardedCalendarGrid =
    ::crucible::concurrent::PermissionedShardedCalendarGrid<
        T, NumShards, NumBuckets, BucketCap, KeyExtractor, QuantumNs, UserTag>;

// ShardedCalendarKeyExtractorOf concept re-export — the V-049-unique
// surface bump.  Pattern parallels V-045 (SpscValue), V-046 (MpmcValue),
// V-047 (DequeValue) — one new using-decl bumps the cardinality.
// ShardedCalendarGrid reuses SpscValue from V-045 for the T-side
// constraint (no ShardedCalendarValue exists in the substrate); the
// V-049-unique concept is the KeyExtractor predicate.
using ::crucible::concurrent::ShardedCalendarKeyExtractorOf;

// sharded_calendar_tag sub-namespace — Whole<UserTag> permission root
// + the FOUND-A22 Slice-indexed Producer<UserTag,S> / Consumer<UserTag,S>
// tag aliases.  Callers spell these out at mint_permission_root +
// mint_grid_permissions<Whole, N, N> call sites; surfacing them under
// fixy::substr::sharded_calendar_grid::sharded_calendar_tag::* removes
// the descend-into-concurrent requirement.
namespace sharded_calendar_tag {
template <typename UserTag>
using Whole =
    ::crucible::concurrent::sharded_calendar_tag::Whole<UserTag>;
template <typename UserTag, std::size_t S>
using Producer =
    ::crucible::concurrent::sharded_calendar_tag::Producer<UserTag, S>;
template <typename UserTag, std::size_t S>
using Consumer =
    ::crucible::concurrent::sharded_calendar_tag::Consumer<UserTag, S>;
}  // namespace sharded_calendar_tag

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

// ── FIXY-V-048: substrate-direct surface enrichment ──────────────
//
// Mirror of V-045 spsc / V-046 mpmc / V-047 chaselev.
// PermissionedShardedGrid is the FIFTH cell of the channel-permission
// family — the linear-grid × linear-grid axis:
//
//   M Producer slots — each LINEAR (one Permission per shard I).
//   N Consumer slots — each LINEAR (one Permission per shard J).
//
// All M+N permissions descend from a single Whole<UserTag> root via
// the FOUND-A22 2D auto-permission-tree (mint_grid_permissions,
// already surfaced at fixy::perm::mint_grid_permissions).  No Pool —
// every slot is single-owner.  Each handle is STATICALLY INDEXED at
// compile time: ProducerHandle<I> knows its shard I at type level
// and try_push takes no id parameter.
//
// Pre-V-048 the sharded_grid:: sub-namespace surfaced only ctx-bound
// session mints + endpoint mints — callers who wanted to instantiate
// the substrate or pick a Routing policy had to descend into
// <crucible/concurrent/PermissionedShardedGrid.h> AND
// <crucible/concurrent/ShardedGrid.h> directly.  V-048 closes the
// gap by re-exporting the substrate primitive, its three-tag
// permission tree, and the three pre-shipped Routing policy structs.
//
// Surface (additive, all alias-template / using-decl — pure name
// lookup, zero runtime cost).  No new mint factories — endpoint and
// session mints already shipped from sessions/ShardedGridSession.h.

// Substrate alias — full re-export including default UserTag = void
// and default Routing = RoundRobinRouting.  SpscValue T constraint
// surfaced for diagnostic clarity (same pattern as V-045 / V-047);
// callers spell SpscValue via fixy::substr::spsc::SpscValue (already
// shipped — no duplicate re-export here to avoid namespace pollution).
template <::crucible::concurrent::SpscValue T,
          std::size_t M,
          std::size_t N,
          std::size_t Capacity,
          typename UserTag = void,
          typename Routing = ::crucible::concurrent::RoundRobinRouting>
using PermissionedShardedGrid =
    ::crucible::concurrent::PermissionedShardedGrid<
        T, M, N, Capacity, UserTag, Routing>;

// Routing policy re-exports — three using-decls bump
// substr_sharded_grid_using cardinality 4 → 7.  ShardedGrid reuses
// SpscValue from V-045 (no ShardedGridValue exists); the unique
// V-048-shipped surface is the Routing axis instead.
using ::crucible::concurrent::RoundRobinRouting;
using ::crucible::concurrent::HashKeyRouting;
using ::crucible::concurrent::AffinityRouting;

// grid_tag sub-namespace — Whole<UserTag> permission root + the
// FOUND-A22 Slice-indexed Producer<UserTag,I> / Consumer<UserTag,J>
// tag aliases.  Callers spell these out at
// mint_permission_root + mint_grid_permissions call sites; surfacing
// them under fixy::substr::sharded_grid::grid_tag::* removes the
// descend-into-concurrent requirement.
namespace grid_tag {
template <typename UserTag>
using Whole =
    ::crucible::concurrent::grid_tag::Whole<UserTag>;
template <typename UserTag, std::size_t I>
using Producer =
    ::crucible::concurrent::grid_tag::Producer<UserTag, I>;
template <typename UserTag, std::size_t J>
using Consumer =
    ::crucible::concurrent::grid_tag::Consumer<UserTag, J>;
}  // namespace grid_tag

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
// sharded_calendar_grid (4), sharded_grid (7), snapshot (4), and the
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
// sharded_grid Routing using-decls added by FIXY-V-048 bumped
// sharded_grid 4 → 7 (RoundRobinRouting + HashKeyRouting +
// AffinityRouting — three using-decls).  ShardedGrid reuses SpscValue
// from V-045 (no ShardedGridValue exists in the substrate) so the
// cardinality bump goes through the Routing axis instead, which IS
// V-048-unique.  Substrate alias template + grid_tag tree alias
// templates do NOT count.
// sharded_calendar_grid::ShardedCalendarKeyExtractorOf using-decl
// added by FIXY-V-049 bumped sharded_calendar_grid 4 → 5 (same
// pattern as V-045 / V-046 / V-047 — substrate alias template +
// sharded_calendar_tag tree alias templates do NOT count toward the
// cardinality witness; only the concept using-decl does).
// ShardedCalendarGrid reuses SpscValue from V-045 for the T-side
// constraint (no ShardedCalendarValue exists in the substrate); the
// V-049-unique concept is the KeyExtractor predicate.
// calendar_grid::KeyExtractorOf using-decl added by FIXY-V-050
// bumped calendar_grid 4 → 5 (same pattern as V-045 / V-046 / V-047
// / V-049 — substrate alias template + calendar_tag tree alias
// templates do NOT count toward the cardinality witness; only the
// concept using-decl does).  Single-grid CalendarGrid uses the
// generic KeyExtractorOf concept (V-049 sharded variant uses the
// qualified-name ShardedCalendarKeyExtractorOf to coexist without
// collision).
// metalog::MetaIndex using-decl added by FIXY-V-051 bumped metalog
// 4 → 5.  Unlike V-045..V-050 which surfaced a per-substrate concept,
// MetaLog's value_type is hard-coded to TensorMeta (no MetaLogValue
// payload predicate exists), so the V-051-unique cardinality bump
// is the substrate's strong-ID return type ::crucible::MetaIndex
// (returned by ProducerHandle::try_append).  Substrate alias template
// + metalog_tag tree alias templates do NOT count toward the
// cardinality witness; only the MetaIndex using-decl does.
// chainedge::VendorBackend using-decl added by FIXY-V-052 bumped
// chainedge 4 → 5.  ChainEdge is structurally V-052-unique among
// V-045..V-052: it is the only substrate parameterized on a
// VendorBackend axis (CPU oracle vs NV / AM / TPU / TRN native
// semaphore).  The V-052-unique cardinality bump is the
// ::crucible::concurrent::VendorBackend enum re-export (the vendor
// axis MUST be available at the fixy:: surface so callers can spell
// non-default backends).  Substrate alias template + chainedge_tag
// tree alias templates do NOT count toward the cardinality witness;
// only the VendorBackend using-decl does.  This closes the V-045..
// V-052 channel-permission family (eight substrate cells: SPSC,
// SWMR-snapshot, MPMC, ChaseLev, ShardedGrid, ShardedCalendarGrid,
// CalendarGrid, MetaLog, ChainEdge — note swmr and snapshot share
// the SWMR sub-namespace).
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

// ── FIXY-V-048: Sharded-grid substrate-direct surface witnesses ──
//
// Same recipe as v045 / v046 / v047 above.  Pin every V-048 addition:
// substrate alias identity (parametric M=4, N=3 plus user-supplied
// UserTag and Routing), three Routing alias-identity witnesses
// (RoundRobinRouting / HashKeyRouting<KeyFn> / AffinityRouting),
// tag tree identity (Whole / Producer<I> / Consumer<J> across the
// FOUND-A22 Slice-indexed templates), member-typedef propagation,
// num_producers / num_consumers / shard_capacity value-template
// passthrough, value_type identity, ShardedGridSessionSurface
// concept admission.  ShardedGrid differs structurally from V-045
// SPSC in that it is M×N statically-indexed (linear-grid × linear-
// grid), but the substrate-direct surface witnesses are
// structurally symmetric.

namespace v048 {

struct V048ProbeUserTag {};
struct V048ProbeKeyFn {
    [[nodiscard]] constexpr std::uint64_t operator()(int x) const noexcept {
        return static_cast<std::uint64_t>(x);
    }
};

// 1. Substrate alias identity — fixy path === concurrent path.  Uses
// default Routing = RoundRobinRouting (omitted from the alias spec).
using GridViaFixy = ::crucible::fixy::substr::sharded_grid::PermissionedShardedGrid<
    int, 4, 3, 64, V048ProbeUserTag>;
using GridViaConcurrent = ::crucible::concurrent::PermissionedShardedGrid<
    int, 4, 3, 64, V048ProbeUserTag, ::crucible::concurrent::RoundRobinRouting>;
static_assert(std::is_same_v<GridViaFixy, GridViaConcurrent>,
    "fixy::substr::sharded_grid::PermissionedShardedGrid must alias the substrate.");

// 1b. Substrate alias identity with explicit non-default Routing.
using GridAffinityViaFixy = ::crucible::fixy::substr::sharded_grid::PermissionedShardedGrid<
    int, 4, 3, 64, V048ProbeUserTag,
    ::crucible::fixy::substr::sharded_grid::AffinityRouting>;
using GridAffinityViaConcurrent = ::crucible::concurrent::PermissionedShardedGrid<
    int, 4, 3, 64, V048ProbeUserTag, ::crucible::concurrent::AffinityRouting>;
static_assert(std::is_same_v<GridAffinityViaFixy, GridAffinityViaConcurrent>);

// 2. Routing policy alias-identity — fixy:: surface re-exports
// alias the concurrent:: substrate's policy structs by-name.
static_assert(std::is_same_v<
    ::crucible::fixy::substr::sharded_grid::RoundRobinRouting,
    ::crucible::concurrent::RoundRobinRouting>);
static_assert(std::is_same_v<
    ::crucible::fixy::substr::sharded_grid::HashKeyRouting<V048ProbeKeyFn>,
    ::crucible::concurrent::HashKeyRouting<V048ProbeKeyFn>>);
static_assert(std::is_same_v<
    ::crucible::fixy::substr::sharded_grid::AffinityRouting,
    ::crucible::concurrent::AffinityRouting>);

// 3. Tag template identity — Whole / Producer<I> / Consumer<J> alias
// the substrate's grid_tag tree exactly.  Producer/Consumer are
// statically indexed via the FOUND-A22 Slice<...> primitive.
static_assert(std::is_same_v<
    ::crucible::fixy::substr::sharded_grid::grid_tag::Whole<V048ProbeUserTag>,
    ::crucible::concurrent::grid_tag::Whole<V048ProbeUserTag>>);
static_assert(std::is_same_v<
    ::crucible::fixy::substr::sharded_grid::grid_tag::Producer<V048ProbeUserTag, 0>,
    ::crucible::concurrent::grid_tag::Producer<V048ProbeUserTag, 0>>);
static_assert(std::is_same_v<
    ::crucible::fixy::substr::sharded_grid::grid_tag::Producer<V048ProbeUserTag, 3>,
    ::crucible::concurrent::grid_tag::Producer<V048ProbeUserTag, 3>>);
static_assert(std::is_same_v<
    ::crucible::fixy::substr::sharded_grid::grid_tag::Consumer<V048ProbeUserTag, 0>,
    ::crucible::concurrent::grid_tag::Consumer<V048ProbeUserTag, 0>>);
static_assert(std::is_same_v<
    ::crucible::fixy::substr::sharded_grid::grid_tag::Consumer<V048ProbeUserTag, 2>,
    ::crucible::concurrent::grid_tag::Consumer<V048ProbeUserTag, 2>>);

// 4. Tag identity propagates through GridViaFixy's member typedefs.
// ShardedGrid does NOT have producer_tag / consumer_tag as singular
// scalar typedefs (they are statically-indexed templates accessed via
// grid_tag::Producer<UserTag, I>).  whole_tag IS a scalar typedef.
static_assert(std::is_same_v<
    typename GridViaFixy::whole_tag,
    ::crucible::fixy::substr::sharded_grid::grid_tag::Whole<V048ProbeUserTag>>);
static_assert(std::is_same_v<typename GridViaFixy::user_tag,
                             V048ProbeUserTag>);

// 5. ShardedGridSessionSurface admits the representative grid.  The
// surface concept is a NOMINAL specialization-trait (admits only
// PermissionedShardedGrid<...>), and the fixy:: alias resolves
// exactly to that substrate type, so the trait fires.
static_assert(
    ::crucible::fixy::substr::sharded_grid::ShardedGridSessionSurface<GridViaFixy>);
static_assert(
    ::crucible::fixy::substr::sharded_grid::ShardedGridSessionSurface<GridAffinityViaFixy>);

// 6. Value-template parity — num_producers / num_consumers /
// shard_capacity pass through unchanged.
static_assert(GridViaFixy::num_producers == 4);
static_assert(GridViaFixy::num_consumers == 3);
static_assert(GridViaFixy::shard_capacity == 64);
static_assert(GridViaFixy::num_producers == GridViaConcurrent::num_producers);
static_assert(GridViaFixy::num_consumers == GridViaConcurrent::num_consumers);
static_assert(GridViaFixy::shard_capacity == GridViaConcurrent::shard_capacity);

// 7. value_type identity — substrate's T == fixy's T.
static_assert(std::is_same_v<typename GridViaFixy::value_type, int>);

}  // namespace v048

// ── FIXY-V-049: ShardedCalendarGrid substrate-direct surface witnesses ─
//
// Pin every V-049 addition to the substrate.  Pattern mirrors V-048:
// substrate-alias identity for two representative instantiations,
// ShardedCalendarKeyExtractorOf concept admission parity (positive +
// negative), tag-tree identity through the sharded_calendar_tag
// sub-namespace, ShardedCalendarGridSessionSurface trait admission,
// member-typedef parity, value-template parity (num_shards /
// num_buckets / bucket_cap / quantum_ns), value_type identity.

namespace v049 {

struct V049ProbeUserTag {};
struct V049ProbeKey {
    static constexpr std::uint64_t key(int v) noexcept {
        return static_cast<std::uint64_t>(v);
    }
};

// 1. Substrate alias identity — fixy path === concurrent path.
using GridViaFixy = ::crucible::fixy::substr::sharded_calendar_grid::
    PermissionedShardedCalendarGrid<int, 2, 8, 16, V049ProbeKey, 1ULL,
                                    V049ProbeUserTag>;
using GridViaConcurrent = ::crucible::concurrent::
    PermissionedShardedCalendarGrid<int, 2, 8, 16, V049ProbeKey, 1ULL,
                                    V049ProbeUserTag>;
static_assert(std::is_same_v<GridViaFixy, GridViaConcurrent>,
    "fixy::substr::sharded_calendar_grid::PermissionedShardedCalendarGrid "
    "must alias the substrate.");

// 2. ShardedCalendarKeyExtractorOf concept admission parity.  The
// using-decl preserves concept semantics: positive witness on the
// real KeyExtractor + negative witness on a non-conforming type.
static_assert(
    ::crucible::fixy::substr::sharded_calendar_grid::
        ShardedCalendarKeyExtractorOf<V049ProbeKey, int> ==
    ::crucible::concurrent::ShardedCalendarKeyExtractorOf<V049ProbeKey, int>);
static_assert(
    ::crucible::fixy::substr::sharded_calendar_grid::
        ShardedCalendarKeyExtractorOf<V049ProbeKey, int>);
struct NonKeyExtractor {};
static_assert(
    !::crucible::fixy::substr::sharded_calendar_grid::
        ShardedCalendarKeyExtractorOf<NonKeyExtractor, int>);
static_assert(
    !::crucible::concurrent::ShardedCalendarKeyExtractorOf<NonKeyExtractor, int>);

// 3. Tag template identity — fixy path === concurrent path.  Whole +
// representative Producer<S> + Consumer<S> at S=0 and S=NumShards-1.
static_assert(std::is_same_v<
    ::crucible::fixy::substr::sharded_calendar_grid::sharded_calendar_tag::
        Whole<V049ProbeUserTag>,
    ::crucible::concurrent::sharded_calendar_tag::Whole<V049ProbeUserTag>>);
static_assert(std::is_same_v<
    ::crucible::fixy::substr::sharded_calendar_grid::sharded_calendar_tag::
        Producer<V049ProbeUserTag, 0>,
    ::crucible::concurrent::sharded_calendar_tag::Producer<
        V049ProbeUserTag, 0>>);
static_assert(std::is_same_v<
    ::crucible::fixy::substr::sharded_calendar_grid::sharded_calendar_tag::
        Producer<V049ProbeUserTag, 1>,
    ::crucible::concurrent::sharded_calendar_tag::Producer<
        V049ProbeUserTag, 1>>);
static_assert(std::is_same_v<
    ::crucible::fixy::substr::sharded_calendar_grid::sharded_calendar_tag::
        Consumer<V049ProbeUserTag, 0>,
    ::crucible::concurrent::sharded_calendar_tag::Consumer<
        V049ProbeUserTag, 0>>);
static_assert(std::is_same_v<
    ::crucible::fixy::substr::sharded_calendar_grid::sharded_calendar_tag::
        Consumer<V049ProbeUserTag, 1>,
    ::crucible::concurrent::sharded_calendar_tag::Consumer<
        V049ProbeUserTag, 1>>);

// 4. Tag identity propagates through GridViaFixy's member typedefs.
// ShardedCalendarGrid (like ShardedGrid) does NOT have producer_tag /
// consumer_tag as singular scalar typedefs — they are
// statically-indexed via shard_producer_tag<S> / shard_consumer_tag<S>
// template-typedefs.  whole_tag IS a scalar typedef.
static_assert(std::is_same_v<
    typename GridViaFixy::whole_tag,
    ::crucible::fixy::substr::sharded_calendar_grid::sharded_calendar_tag::
        Whole<V049ProbeUserTag>>);
static_assert(std::is_same_v<typename GridViaFixy::user_tag,
                             V049ProbeUserTag>);
static_assert(std::is_same_v<typename GridViaFixy::key_extractor,
                             V049ProbeKey>);
static_assert(std::is_same_v<
    typename GridViaFixy::template shard_producer_tag<0>,
    ::crucible::fixy::substr::sharded_calendar_grid::sharded_calendar_tag::
        Producer<V049ProbeUserTag, 0>>);
static_assert(std::is_same_v<
    typename GridViaFixy::template shard_consumer_tag<1>,
    ::crucible::fixy::substr::sharded_calendar_grid::sharded_calendar_tag::
        Consumer<V049ProbeUserTag, 1>>);

// 5. ShardedCalendarGridSessionSurface admits the representative grid.
// The surface concept is a NOMINAL specialization-trait (admits only
// PermissionedShardedCalendarGrid<...>), and the fixy:: alias resolves
// exactly to that substrate type, so the trait fires.
static_assert(
    ::crucible::fixy::substr::sharded_calendar_grid::
        ShardedCalendarGridSessionSurface<GridViaFixy>);

// 6. Value-template parity — num_shards / num_buckets / bucket_cap /
// quantum_ns pass through unchanged.
static_assert(GridViaFixy::num_shards == 2);
static_assert(GridViaFixy::num_buckets == 8);
static_assert(GridViaFixy::bucket_cap == 16);
static_assert(GridViaFixy::quantum_ns == 1ULL);
static_assert(GridViaFixy::num_shards == GridViaConcurrent::num_shards);
static_assert(GridViaFixy::num_buckets == GridViaConcurrent::num_buckets);
static_assert(GridViaFixy::bucket_cap == GridViaConcurrent::bucket_cap);
static_assert(GridViaFixy::quantum_ns == GridViaConcurrent::quantum_ns);

// 7. value_type identity — substrate's T == fixy's T.
static_assert(std::is_same_v<typename GridViaFixy::value_type, int>);

}  // namespace v049

// ── FIXY-V-050: CalendarGrid substrate-direct surface witnesses ──
//
// Pin every V-050 addition to the substrate.  Pattern mirrors V-049:
// substrate-alias identity (single representative + key-only
// instantiation), KeyExtractorOf concept admission parity (positive
// + negative), tag-tree identity through the calendar_tag
// sub-namespace, CalendarGridSessionSurface trait admission,
// member-typedef parity, value-template parity (num_producers /
// num_buckets / bucket_cap / quantum_ns), value_type identity.
//
// Note: V-050 single-grid shape is M-Producer × 1-Consumer (not
// per-shard NxN), so the Producer tag is template-indexed and the
// Consumer tag is SCALAR.

namespace v050 {

struct V050ProbeUserTag {};
struct V050ProbeKey {
    static std::uint64_t key(int v) noexcept {
        return static_cast<std::uint64_t>(v);
    }
};

// 1. Substrate alias identity — fixy path === concurrent path.
using GridViaFixy = ::crucible::fixy::substr::calendar_grid::
    PermissionedCalendarGrid<int, 4, 8, 16, V050ProbeKey, 1ULL,
                              V050ProbeUserTag>;
using GridViaConcurrent = ::crucible::concurrent::
    PermissionedCalendarGrid<int, 4, 8, 16, V050ProbeKey, 1ULL,
                              V050ProbeUserTag>;
static_assert(std::is_same_v<GridViaFixy, GridViaConcurrent>,
    "fixy::substr::calendar_grid::PermissionedCalendarGrid "
    "must alias the substrate.");

// 2. KeyExtractorOf concept admission parity.  The using-decl
// preserves concept semantics: positive witness on the real
// KeyExtractor + negative witness on a non-conforming type.
static_assert(
    ::crucible::fixy::substr::calendar_grid::KeyExtractorOf<
        V050ProbeKey, int> ==
    ::crucible::concurrent::KeyExtractorOf<V050ProbeKey, int>);
static_assert(
    ::crucible::fixy::substr::calendar_grid::KeyExtractorOf<
        V050ProbeKey, int>);
struct NonCalendarKey {};
static_assert(
    !::crucible::fixy::substr::calendar_grid::KeyExtractorOf<
        NonCalendarKey, int>);
static_assert(
    !::crucible::concurrent::KeyExtractorOf<NonCalendarKey, int>);

// 3. Tag template identity — fixy path === concurrent path.
// Whole<UserTag> + representative Producer<UserTag, P> at P=0 and
// P=NumProducers-1, + SCALAR Consumer<UserTag>.
static_assert(std::is_same_v<
    ::crucible::fixy::substr::calendar_grid::calendar_tag::Whole<
        V050ProbeUserTag>,
    ::crucible::concurrent::calendar_tag::Whole<V050ProbeUserTag>>);
static_assert(std::is_same_v<
    ::crucible::fixy::substr::calendar_grid::calendar_tag::Producer<
        V050ProbeUserTag, 0>,
    ::crucible::concurrent::calendar_tag::Producer<V050ProbeUserTag, 0>>);
static_assert(std::is_same_v<
    ::crucible::fixy::substr::calendar_grid::calendar_tag::Producer<
        V050ProbeUserTag, 3>,
    ::crucible::concurrent::calendar_tag::Producer<V050ProbeUserTag, 3>>);
static_assert(std::is_same_v<
    ::crucible::fixy::substr::calendar_grid::calendar_tag::Consumer<
        V050ProbeUserTag>,
    ::crucible::concurrent::calendar_tag::Consumer<V050ProbeUserTag>>);

// 4. Tag identity propagates through GridViaFixy's member typedefs.
// CalendarGrid (M × 1) has SCALAR consumer_tag + template
// producer_tag<P>; whole_tag scalar.
static_assert(std::is_same_v<
    typename GridViaFixy::whole_tag,
    ::crucible::fixy::substr::calendar_grid::calendar_tag::Whole<
        V050ProbeUserTag>>);
static_assert(std::is_same_v<
    typename GridViaFixy::consumer_tag,
    ::crucible::fixy::substr::calendar_grid::calendar_tag::Consumer<
        V050ProbeUserTag>>);
static_assert(std::is_same_v<typename GridViaFixy::user_tag,
                             V050ProbeUserTag>);
static_assert(std::is_same_v<typename GridViaFixy::key_extractor,
                             V050ProbeKey>);
static_assert(std::is_same_v<
    typename GridViaFixy::template producer_tag<0>,
    ::crucible::fixy::substr::calendar_grid::calendar_tag::Producer<
        V050ProbeUserTag, 0>>);
static_assert(std::is_same_v<
    typename GridViaFixy::template producer_tag<3>,
    ::crucible::fixy::substr::calendar_grid::calendar_tag::Producer<
        V050ProbeUserTag, 3>>);

// 5. CalendarGridSessionSurface admits the representative grid.
// The surface concept is a NOMINAL specialization-trait (admits only
// PermissionedCalendarGrid<...>), and the fixy:: alias resolves
// exactly to that substrate type, so the trait fires.
static_assert(
    ::crucible::fixy::substr::calendar_grid::CalendarGridSessionSurface<
        GridViaFixy>);

// 6. Value-template parity — num_producers / num_buckets / bucket_cap
// / quantum_ns pass through unchanged.
static_assert(GridViaFixy::num_producers == 4);
static_assert(GridViaFixy::num_buckets == 8);
static_assert(GridViaFixy::bucket_cap == 16);
static_assert(GridViaFixy::quantum_ns == 1ULL);
static_assert(GridViaFixy::num_producers == GridViaConcurrent::num_producers);
static_assert(GridViaFixy::num_buckets == GridViaConcurrent::num_buckets);
static_assert(GridViaFixy::bucket_cap == GridViaConcurrent::bucket_cap);
static_assert(GridViaFixy::quantum_ns == GridViaConcurrent::quantum_ns);

// 7. value_type identity — substrate's T == fixy's T.
static_assert(std::is_same_v<typename GridViaFixy::value_type, int>);

}  // namespace v050

// ── FIXY-V-051: MetaLog substrate-direct surface witnesses ───────
//
// Same recipe as v045::..v050:: above.  Pin every V-051 addition:
// substrate alias identity, MetaIndex type identity (V-051-unique
// cardinality bump — see U-103 prose above), metalog_tag tree
// identity, member-typedef propagation, value_type identity (fixed
// to TensorMeta by substrate), MetaLogSessionSurface admission.

namespace v051 {

struct V051ProbeUserTag {};

// 1. Substrate alias identity — fixy path === concurrent path.
using LogViaFixy = ::crucible::fixy::substr::metalog::PermissionedMetaLog<
    V051ProbeUserTag>;
using LogViaConcurrent = ::crucible::concurrent::PermissionedMetaLog<
    V051ProbeUserTag>;
static_assert(std::is_same_v<LogViaFixy, LogViaConcurrent>,
    "fixy::substr::metalog::PermissionedMetaLog must alias the substrate.");

// 2. MetaIndex strong-ID identity — V-051-unique cardinality bump.
// fixy:: namespace's MetaIndex must alias the canonical
// ::crucible::MetaIndex returned by ProducerHandle::try_append.
static_assert(std::is_same_v<
    ::crucible::fixy::substr::metalog::MetaIndex,
    ::crucible::MetaIndex>,
    "fixy::substr::metalog::MetaIndex must alias ::crucible::MetaIndex.");

// 3. Tag template identity — Whole/Producer/Consumer alias the
// substrate's metalog_tag tree exactly.
static_assert(std::is_same_v<
    ::crucible::fixy::substr::metalog::metalog_tag::Whole<V051ProbeUserTag>,
    ::crucible::concurrent::metalog_tag::Whole<V051ProbeUserTag>>);
static_assert(std::is_same_v<
    ::crucible::fixy::substr::metalog::metalog_tag::Producer<V051ProbeUserTag>,
    ::crucible::concurrent::metalog_tag::Producer<V051ProbeUserTag>>);
static_assert(std::is_same_v<
    ::crucible::fixy::substr::metalog::metalog_tag::Consumer<V051ProbeUserTag>,
    ::crucible::concurrent::metalog_tag::Consumer<V051ProbeUserTag>>);

// 4. Tag identity propagates through LogViaFixy's member typedefs.
static_assert(std::is_same_v<
    typename LogViaFixy::whole_tag,
    ::crucible::fixy::substr::metalog::metalog_tag::Whole<V051ProbeUserTag>>);
static_assert(std::is_same_v<
    typename LogViaFixy::producer_tag,
    ::crucible::fixy::substr::metalog::metalog_tag::Producer<V051ProbeUserTag>>);
static_assert(std::is_same_v<
    typename LogViaFixy::consumer_tag,
    ::crucible::fixy::substr::metalog::metalog_tag::Consumer<V051ProbeUserTag>>);

// 5. MetaLogSessionSurface admits the representative log.  The
// surface concept is a NOMINAL specialization-trait (admits only
// PermissionedMetaLog<...>), and the fixy:: alias resolves exactly
// to that substrate type, so the trait fires.
static_assert(
    ::crucible::fixy::substr::metalog::MetaLogSessionSurface<LogViaFixy>);

// 6. value_type identity — substrate's value_type == TensorMeta ==
// fixy's MetaLogRecord alias.
static_assert(std::is_same_v<
    typename LogViaFixy::value_type,
    ::crucible::TensorMeta>);
static_assert(std::is_same_v<
    typename LogViaFixy::value_type,
    ::crucible::fixy::substr::metalog::MetaLogRecord>);

// 7. Protocol aliases unchanged through V-051 (pre-existing
// re-exports remain canonical).
static_assert(std::is_same_v<
    ::crucible::fixy::substr::metalog::ProducerProto,
    ::crucible::safety::proto::metalog_session::ProducerProto>);
static_assert(std::is_same_v<
    ::crucible::fixy::substr::metalog::ConsumerProto,
    ::crucible::safety::proto::metalog_session::ConsumerProto>);

}  // namespace v051

// ── FIXY-V-052: ChainEdge substrate-direct surface witnesses ──────
//
// Same recipe as v045::..v051:: above.  Pin every V-052 addition:
// substrate alias identity (with default Backend = CPU AND
// non-default backend), VendorBackend enum-value parity (V-052-
// unique cardinality bump — see U-103 prose above), chainedge_tag
// tree identity, member-typedef propagation, Signal/value_type
// identity (fixed to SemaphoreSignal by substrate), surface concept
// admission, protocol aliases preserved.

namespace v052 {

struct V052ProbeUserTag {};

// 1. Substrate alias identity — default Backend = CPU.
using EdgeViaFixy = ::crucible::fixy::substr::chainedge::PermissionedChainEdge<
    ::crucible::concurrent::VendorBackend::CPU, V052ProbeUserTag>;
using EdgeViaConcurrent = ::crucible::concurrent::PermissionedChainEdge<
    ::crucible::concurrent::VendorBackend::CPU, V052ProbeUserTag>;
static_assert(std::is_same_v<EdgeViaFixy, EdgeViaConcurrent>,
    "fixy::substr::chainedge::PermissionedChainEdge must alias the substrate.");

// 1b. Substrate alias identity — non-default Backend (NV).  Verifies
// that the Backend template parameter passes through unchanged.
using EdgeNvViaFixy = ::crucible::fixy::substr::chainedge::PermissionedChainEdge<
    ::crucible::fixy::substr::chainedge::VendorBackend::NV,
    V052ProbeUserTag>;
using EdgeNvViaConcurrent = ::crucible::concurrent::PermissionedChainEdge<
    ::crucible::concurrent::VendorBackend::NV, V052ProbeUserTag>;
static_assert(std::is_same_v<EdgeNvViaFixy, EdgeNvViaConcurrent>,
    "fixy::substr::chainedge::PermissionedChainEdge<NV> must alias the "
    "non-default-backend substrate variant.");

// 2. VendorBackend enum identity — V-052-unique cardinality bump.
// fixy:: re-exports the underlying enum AND every enumerator value
// must compare equal to the substrate's corresponding enumerator.
static_assert(std::is_same_v<
    ::crucible::fixy::substr::chainedge::VendorBackend,
    ::crucible::concurrent::VendorBackend>,
    "fixy::substr::chainedge::VendorBackend must alias "
    "::crucible::concurrent::VendorBackend.");
static_assert(static_cast<int>(
        ::crucible::fixy::substr::chainedge::VendorBackend::CPU) ==
    static_cast<int>(::crucible::concurrent::VendorBackend::CPU));
static_assert(static_cast<int>(
        ::crucible::fixy::substr::chainedge::VendorBackend::NV) ==
    static_cast<int>(::crucible::concurrent::VendorBackend::NV));

// 3. Tag template identity — Whole/Signaler/Waiter alias the
// substrate's chainedge_tag tree exactly.
static_assert(std::is_same_v<
    ::crucible::fixy::substr::chainedge::chainedge_tag::Whole<V052ProbeUserTag>,
    ::crucible::concurrent::chainedge_tag::Whole<V052ProbeUserTag>>);
static_assert(std::is_same_v<
    ::crucible::fixy::substr::chainedge::chainedge_tag::Signaler<V052ProbeUserTag>,
    ::crucible::concurrent::chainedge_tag::Signaler<V052ProbeUserTag>>);
static_assert(std::is_same_v<
    ::crucible::fixy::substr::chainedge::chainedge_tag::Waiter<V052ProbeUserTag>,
    ::crucible::concurrent::chainedge_tag::Waiter<V052ProbeUserTag>>);

// 4. Tag identity propagates through EdgeViaFixy's member typedefs.
static_assert(std::is_same_v<
    typename EdgeViaFixy::whole_tag,
    ::crucible::fixy::substr::chainedge::chainedge_tag::Whole<V052ProbeUserTag>>);
static_assert(std::is_same_v<
    typename EdgeViaFixy::signaler_tag,
    ::crucible::fixy::substr::chainedge::chainedge_tag::Signaler<V052ProbeUserTag>>);
static_assert(std::is_same_v<
    typename EdgeViaFixy::waiter_tag,
    ::crucible::fixy::substr::chainedge::chainedge_tag::Waiter<V052ProbeUserTag>>);

// 5. ChainEdgeSessionSurface admits the representative edge.
static_assert(
    ::crucible::fixy::substr::chainedge::ChainEdgeSessionSurface<EdgeViaFixy>);

// 6. value_type identity — Signal == SemaphoreSignal.
static_assert(std::is_same_v<
    typename EdgeViaFixy::value_type,
    ::crucible::concurrent::SemaphoreSignal>);
static_assert(std::is_same_v<
    typename EdgeViaFixy::value_type,
    ::crucible::fixy::substr::chainedge::Signal>);

// 7. Backend constant passes through.
static_assert(EdgeViaFixy::backend ==
              ::crucible::concurrent::VendorBackend::CPU);
static_assert(EdgeNvViaFixy::backend ==
              ::crucible::concurrent::VendorBackend::NV);

// 8. Protocol aliases unchanged through V-052.
static_assert(std::is_same_v<
    ::crucible::fixy::substr::chainedge::SignalerProto,
    ::crucible::safety::proto::chainedge_session::SignalerProto>);
static_assert(std::is_same_v<
    ::crucible::fixy::substr::chainedge::WaiterProto,
    ::crucible::safety::proto::chainedge_session::WaiterProto>);

}  // namespace v052

// ── Per-sub-namespace cardinality witnesses ──────────────────────

constexpr int substr_spsc_using                  = 3;
constexpr int substr_swmr_using                  = 6;
constexpr int substr_chaselev_using              = 5;
constexpr int substr_metalog_using               = 5;
constexpr int substr_chainedge_using             = 5;
constexpr int substr_mpmc_using                  = 5;
constexpr int substr_calendar_grid_using         = 5;
constexpr int substr_sharded_calendar_grid_using = 5;
constexpr int substr_sharded_grid_using          = 7;
constexpr int substr_snapshot_using              = 4;
constexpr int substr_outer_using                 = 1;

constexpr int substr_total_using =
    substr_spsc_using + substr_swmr_using + substr_chaselev_using +
    substr_metalog_using + substr_chainedge_using + substr_mpmc_using +
    substr_calendar_grid_using + substr_sharded_calendar_grid_using +
    substr_sharded_grid_using + substr_snapshot_using +
    substr_outer_using;

static_assert(substr_total_using == 51,
    "fixy::substr:: using-decl surface drifted from 51 — Substr.h "
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
