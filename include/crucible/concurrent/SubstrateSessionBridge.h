#pragma once

// ── crucible::concurrent::SubstrateSessionBridge ────────────────────
//
// The substrate→session bridge.  Generalizes
// `sessions/SpscSession.h::mint_producer_session` /
// `mint_consumer_session` to ALL Permissioned* substrate types via the
// `default_proto_for<Substr, Dir>` metafunction.  Implements the
// Universal Mint Pattern (CLAUDE.md §XXI) for substrate-shaped
// sessions:
//
//     mint_substrate_session<Substr, Dir, LoopCtx>(ctx, channel, perm)
//                                                 → PSH<default_proto, ...>
//
// ── What this header ships ──────────────────────────────────────────
//
//   Direction                — enum tag for the per-substrate role
//                              (Producer / Consumer / SwmrWriter /
//                              SwmrReader / Owner / Thief).
//
//   handle_for<Substr, Dir>  — metafunction: substrate × direction →
//                              the concrete handle type
//                              (Substr::ProducerHandle, etc.).
//
//   default_proto_for<Substr, Dir>
//                            — metafunction: substrate × direction →
//                              the canonical session protocol
//                              (Loop<Send<T, Continue>> /
//                              Loop<Recv<T, Continue>>).
//
//   mint_substrate_session<Substr, Dir, LoopCtx>(ctx, channel, perm)
//                            — factory.  Validates SubstrateFitsCtx
//                              Residency at construction; returns the
//                              standard PermissionedSessionHandle from
//                              `sessions/PermissionedSession.h` typed
//                              over `default_proto_for_t<Substr, Dir>`
//                              with EmptyPermSet (the substrate's
//                              Permission discipline already enforces
//                              single-producer-or-multi-producer
//                              linearity at the handle layer).  LoopCtx
//                              defaults to void for vendor-free payloads;
//                              vendor-pinned payloads require an explicit
//                              proto::VendorCtx<V>.
//
// ── Why this extends SpscSession.h ─────────────────────────────────
//
// SpscSession.h ships the SPSC-specific worked example.  This header
// generalizes the same pattern to every supported substrate via the
// metafunction trio above.  Production callers can now write:
//
//     auto sess = mint_substrate_session<TraceRingSubstrate,
//                                         Direction::Producer>(
//         eff::HotFgCtx{}, *trace_ring, std::move(producer_perm));
//
// — one line; substrate × direction × ctx all checked at construction;
// returns the canonical PSH ready for send/recv at full speed.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   TypeSafe — every metafunction is closed over recognized
//              (Substrate, Direction) pairs; non-axis combinations
//              fall through the primary undefined template.
//   InitSafe — pure metafunctions during type resolution.
//   DetSafe  — consteval throughout; same inputs → same handle type.
//   MemSafe  — Permission token is consumed by mint at the boundary;
//              the underlying handle's reference-to-channel
//              guarantees the channel's address is the Pinned identity.
//   Runtime cost: zero.  The mint factory inlines through to the
//                 existing `mint_permissioned_session<...>` factory
//                 from `sessions/PermissionedSession.h`.
//
// ── Status ──────────────────────────────────────────────────────────
//
// v1 covers: SPSC, MPSC, MPMC, Snapshot, ChaseLevDeque.
// v2 deferred: ShardedGrid / CalendarGrid (multi-shard substrates
//              need per-shard mint factories).

#include <crucible/concurrent/PermissionedMpmcChannel.h>
#include <crucible/concurrent/PermissionedMpscChannel.h>
#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/concurrent/Substrate.h>
#include <crucible/concurrent/SubstrateCtxFit.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/sessions/ChaseLevDequeSession.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionMint.h>

#include <cstdint>
#include <source_location>
#include <type_traits>
#include <utility>

namespace crucible::concurrent {

// ── Direction enum ──────────────────────────────────────────────────
//
// Per-substrate role tag.  Each (Substrate, Direction) pair selects
// one concrete handle type via handle_for<S, D> below.

enum class Direction : std::uint8_t {
    Producer    = 0,   // SPSC/MPSC/MPMC: try_push side
    Consumer    = 1,   // SPSC/MPSC/MPMC: try_pop side
    SwmrWriter  = 2,   // Snapshot: publish side
    SwmrReader  = 3,   // Snapshot: load side
    Owner       = 4,   // ChaseLevDeque: push_bottom + pop_bottom
    Thief       = 5,   // ChaseLevDeque: steal_top only
};

// ── handle_for<Substr, Dir>: substrate × direction → handle ────────
//
// Primary template undefined; partial specs map every recognized
// (Substrate, Direction) pair.

template <class Substr, Direction Dir>
struct handle_for;

// SPSC: Producer/Consumer
template <class T, std::size_t Cap, class UserTag>
struct handle_for<PermissionedSpscChannel<T, Cap, UserTag>, Direction::Producer> {
    using type = typename PermissionedSpscChannel<T, Cap, UserTag>::ProducerHandle;
};
template <class T, std::size_t Cap, class UserTag>
struct handle_for<PermissionedSpscChannel<T, Cap, UserTag>, Direction::Consumer> {
    using type = typename PermissionedSpscChannel<T, Cap, UserTag>::ConsumerHandle;
};

// MPSC: Producer/Consumer
template <class T, std::size_t Cap, class UserTag>
struct handle_for<PermissionedMpscChannel<T, Cap, UserTag>, Direction::Producer> {
    using type = typename PermissionedMpscChannel<T, Cap, UserTag>::ProducerHandle;
};
template <class T, std::size_t Cap, class UserTag>
struct handle_for<PermissionedMpscChannel<T, Cap, UserTag>, Direction::Consumer> {
    using type = typename PermissionedMpscChannel<T, Cap, UserTag>::ConsumerHandle;
};

// MPMC: Producer/Consumer (uses the Active-specialized aliases from
// PermissionedMpmcChannel.h; the non-Active Closed handles aren't
// session-bridge-able since they're terminal).
template <class T, std::size_t Cap, class UserTag>
struct handle_for<PermissionedMpmcChannel<T, Cap, UserTag>, Direction::Producer> {
    using type = typename PermissionedMpmcChannel<T, Cap, UserTag>::ProducerHandle;
};
template <class T, std::size_t Cap, class UserTag>
struct handle_for<PermissionedMpmcChannel<T, Cap, UserTag>, Direction::Consumer> {
    using type = typename PermissionedMpmcChannel<T, Cap, UserTag>::ConsumerHandle;
};

// Snapshot: WriterHandle / ReaderHandle (one writer, many readers)
template <class T, class UserTag>
struct handle_for<PermissionedSnapshot<T, UserTag>, Direction::SwmrWriter> {
    using type = typename PermissionedSnapshot<T, UserTag>::WriterHandle;
};
template <class T, class UserTag>
struct handle_for<PermissionedSnapshot<T, UserTag>, Direction::SwmrReader> {
    using type = typename PermissionedSnapshot<T, UserTag>::ReaderHandle;
};

// ChaseLevDeque: OwnerHandle / ThiefHandle
template <class T, std::size_t Cap, class UserTag>
struct handle_for<PermissionedChaseLevDeque<T, Cap, UserTag>, Direction::Owner> {
    using type = typename PermissionedChaseLevDeque<T, Cap, UserTag>::OwnerHandle;
};
template <class T, std::size_t Cap, class UserTag>
struct handle_for<PermissionedChaseLevDeque<T, Cap, UserTag>, Direction::Thief> {
    using type = typename PermissionedChaseLevDeque<T, Cap, UserTag>::ThiefHandle;
};

template <class Substr, Direction Dir>
using handle_for_t = typename handle_for<Substr, Dir>::type;

// ── default_proto_for<Substr, Dir>: substrate × direction → proto ──
//
// The canonical session protocol for each (Substrate, Direction) pair.
// Streaming substrates (SPSC/MPSC/MPMC/Snapshot) all use the same
// shape: Loop<Send<T, Continue>> for the "push" side,
// Loop<Recv<T, Continue>> for the "pop" side.
//
// Loop without an exit branch is the documented infinite-loop pattern
// (per SpscSession.h:140-146).  Shutdown is via detach with a typed
// reason; the channel's Permission discipline at the handle layer
// handles single-producer-or-multi-producer semantics independently.

template <class Substr, Direction Dir>
struct default_proto_for;

// Producer-side: streaming send loop
template <class T, std::size_t Cap, class UserTag>
struct default_proto_for<PermissionedSpscChannel<T, Cap, UserTag>, Direction::Producer> {
    using type = ::crucible::safety::proto::Loop<
        ::crucible::safety::proto::Send<T,
            ::crucible::safety::proto::Continue>>;
};
template <class T, std::size_t Cap, class UserTag>
struct default_proto_for<PermissionedMpscChannel<T, Cap, UserTag>, Direction::Producer> {
    using type = ::crucible::safety::proto::Loop<
        ::crucible::safety::proto::Send<T,
            ::crucible::safety::proto::Continue>>;
};
template <class T, std::size_t Cap, class UserTag>
struct default_proto_for<PermissionedMpmcChannel<T, Cap, UserTag>, Direction::Producer> {
    using type = ::crucible::safety::proto::Loop<
        ::crucible::safety::proto::Send<T,
            ::crucible::safety::proto::Continue>>;
};

// Consumer-side: streaming recv loop
template <class T, std::size_t Cap, class UserTag>
struct default_proto_for<PermissionedSpscChannel<T, Cap, UserTag>, Direction::Consumer> {
    using type = ::crucible::safety::proto::Loop<
        ::crucible::safety::proto::Recv<T,
            ::crucible::safety::proto::Continue>>;
};
template <class T, std::size_t Cap, class UserTag>
struct default_proto_for<PermissionedMpscChannel<T, Cap, UserTag>, Direction::Consumer> {
    using type = ::crucible::safety::proto::Loop<
        ::crucible::safety::proto::Recv<T,
            ::crucible::safety::proto::Continue>>;
};
template <class T, std::size_t Cap, class UserTag>
struct default_proto_for<PermissionedMpmcChannel<T, Cap, UserTag>, Direction::Consumer> {
    using type = ::crucible::safety::proto::Loop<
        ::crucible::safety::proto::Recv<T,
            ::crucible::safety::proto::Continue>>;
};

// Snapshot SwmrWriter: publish stream (semantically Send-typed)
template <class T, class UserTag>
struct default_proto_for<PermissionedSnapshot<T, UserTag>, Direction::SwmrWriter> {
    using type = ::crucible::safety::proto::Loop<
        ::crucible::safety::proto::Send<T,
            ::crucible::safety::proto::Continue>>;
};

// Snapshot SwmrReader: load stream (semantically Recv-typed)
template <class T, class UserTag>
struct default_proto_for<PermissionedSnapshot<T, UserTag>, Direction::SwmrReader> {
    using type = ::crucible::safety::proto::Loop<
        ::crucible::safety::proto::Recv<T,
            ::crucible::safety::proto::Continue>>;
};

// ChaseLev owner: local Select chooses push_bottom or pop_bottom.
template <class T, std::size_t Cap, class UserTag>
struct default_proto_for<PermissionedChaseLevDeque<T, Cap, UserTag>,
                         Direction::Owner> {
    using type =
        ::crucible::safety::proto::chaselev_session::OwnerProto<T>;
};

// ChaseLev thief: Recv-only borrowed steal from top.
template <class T, std::size_t Cap, class UserTag>
struct default_proto_for<PermissionedChaseLevDeque<T, Cap, UserTag>,
                         Direction::Thief> {
    using type =
        ::crucible::safety::proto::chaselev_session::ThiefProto<
            T, typename PermissionedChaseLevDeque<T, Cap, UserTag>::thief_tag>;
};

template <class Substr, Direction Dir>
using default_proto_for_t = typename default_proto_for<Substr, Dir>::type;

// ── Recognition concept ────────────────────────────────────────────
//
// IsBridgeableDirection<S, D>: true iff (S, D) has a default_proto_for
// specialization AND a handle_for specialization — i.e., the substrate
// can be turned into a typed session in this direction.

namespace detail {

template <class Substr, Direction Dir>
concept HasHandleFor = requires { typename handle_for<Substr, Dir>::type; };

template <class Substr, Direction Dir>
concept HasDefaultProtoFor = requires { typename default_proto_for<Substr, Dir>::type; };

}  // namespace detail

template <class Substr, Direction Dir>
concept IsBridgeableDirection =
    IsSubstrate<Substr>
 && detail::HasHandleFor<Substr, Dir>
 && detail::HasDefaultProtoFor<Substr, Dir>;

// ── mint_substrate_session<Substr, Dir, LoopCtx>(ctx, handle) ──────
//
// The Universal Mint factory.  Validates:
//   * Substrate ↔ Ctx residency fit (SubstrateFitsCtxResidency).
//     Post-#861 this checks per_call_working_set_v<S> against the
//     ctx's residency tier — the HOT-PATH access pattern, NOT total
//     channel storage.  Large-N rings compose honestly with hot
//     ctxs (the producer/consumer touches O(1) cache lines per call
//     regardless of capacity).
//   * Substrate × Direction is bridgeable (has handle_for and
//     default_proto_for specializations)
//   * default protocol's Vendor<T> payloads are admitted by LoopCtx.
//     Raw LoopCtx = void is allowed only for vendor-free payloads.
//     VendorCtx<Portable> is explicit cross-vendor; VendorCtx<None>
//     is rejected as uninitialized.
//
// Takes the handle BY REFERENCE (the handle has a reference member to
// its channel; can't be move-assigned, must be bound to enclosing
// scope).  Internally takes its address and forwards as Resource =
// Handle*, mirroring the SpscSession.h pattern.
//
// Returns the standard PermissionedSessionHandle from
// sessions/PermissionedSession.h, typed over default_proto_for_t<S,D>
// with EmptyPermSet.  Substrate's Permission discipline at the handle
// layer enforces single-producer-or-multi-producer semantics
// independently of the wire-permission flow.

template <class Substr,
          Direction Dir,
          typename LoopCtx = void,
          ::crucible::effects::IsExecCtx Ctx>
    requires IsBridgeableDirection<Substr, Dir>
          && SubstrateFitsCtxResidency<Substr, Ctx>
          && ::crucible::safety::proto::CtxFitsPermissionedProtocol<
                 default_proto_for_t<Substr, Dir>, Ctx,
                 ::crucible::safety::proto::EmptyPermSet, LoopCtx>
[[nodiscard]] constexpr auto
mint_substrate_session(Ctx const&, handle_for_t<Substr, Dir>& handle) noexcept
{
    using Proto = default_proto_for_t<Substr, Dir>;
    using Handle = handle_for_t<Substr, Dir>;
    return ::crucible::safety::proto::detail::
        mint_permissioned_session_with_loc<
            Proto,
            ::crucible::safety::proto::EmptyPermSet,
            Handle*,
            LoopCtx>(&handle, std::source_location::current());
}

// ── Self-test block ─────────────────────────────────────────────────
namespace detail::substrate_session_bridge_self_test {

namespace eff   = ::crucible::effects;
namespace proto = ::crucible::safety::proto;

struct UserTag {};

// ── handle_for<> resolves to the right nested handle type ──────────

using Spsc      = PermissionedSpscChannel<int, 64, UserTag>;
using Mpsc      = PermissionedMpscChannel<int, 64, UserTag>;
using Mpmc      = PermissionedMpmcChannel<int, 64, UserTag>;
using SnapT     = PermissionedSnapshot<int, UserTag>;
using DequeT    = PermissionedChaseLevDeque<int, 64, UserTag>;
using NvInt     = ::crucible::safety::Vendor<proto::VendorBackend::NV, int>;
using AmdInt    = ::crucible::safety::Vendor<proto::VendorBackend::AMD, int>;
using NvSpsc    = PermissionedSpscChannel<NvInt, 64, UserTag>;
using AmdSpsc   = PermissionedSpscChannel<AmdInt, 64, UserTag>;

static_assert(std::is_same_v<handle_for_t<Spsc,  Direction::Producer>,
                              typename Spsc::ProducerHandle>);
static_assert(std::is_same_v<handle_for_t<Spsc,  Direction::Consumer>,
                              typename Spsc::ConsumerHandle>);
static_assert(std::is_same_v<handle_for_t<Mpsc,  Direction::Producer>,
                              typename Mpsc::ProducerHandle>);
static_assert(std::is_same_v<handle_for_t<Mpsc,  Direction::Consumer>,
                              typename Mpsc::ConsumerHandle>);
static_assert(std::is_same_v<handle_for_t<Mpmc,  Direction::Producer>,
                              typename Mpmc::ProducerHandle>);
static_assert(std::is_same_v<handle_for_t<Mpmc,  Direction::Consumer>,
                              typename Mpmc::ConsumerHandle>);
static_assert(std::is_same_v<handle_for_t<SnapT, Direction::SwmrWriter>,
                              typename SnapT::WriterHandle>);
static_assert(std::is_same_v<handle_for_t<SnapT, Direction::SwmrReader>,
                              typename SnapT::ReaderHandle>);
static_assert(std::is_same_v<handle_for_t<DequeT, Direction::Owner>,
                              typename DequeT::OwnerHandle>);
static_assert(std::is_same_v<handle_for_t<DequeT, Direction::Thief>,
                              typename DequeT::ThiefHandle>);

// ── default_proto_for<> resolves to the canonical Loop<Send/Recv> ──

static_assert(std::is_same_v<
    default_proto_for_t<Spsc, Direction::Producer>,
    proto::Loop<proto::Send<int, proto::Continue>>>);
static_assert(std::is_same_v<
    default_proto_for_t<Spsc, Direction::Consumer>,
    proto::Loop<proto::Recv<int, proto::Continue>>>);
static_assert(std::is_same_v<
    default_proto_for_t<Mpsc, Direction::Producer>,
    proto::Loop<proto::Send<int, proto::Continue>>>);
static_assert(std::is_same_v<
    default_proto_for_t<Mpmc, Direction::Consumer>,
    proto::Loop<proto::Recv<int, proto::Continue>>>);
static_assert(std::is_same_v<
    default_proto_for_t<SnapT, Direction::SwmrWriter>,
    proto::Loop<proto::Send<int, proto::Continue>>>);
static_assert(std::is_same_v<
    default_proto_for_t<DequeT, Direction::Owner>,
    proto::chaselev_session::OwnerProto<int>>);
static_assert(std::is_same_v<
    default_proto_for_t<DequeT, Direction::Thief>,
    proto::chaselev_session::ThiefProto<int, DequeT::thief_tag>>);

// ── IsBridgeableDirection concept ──────────────────────────────────

static_assert( IsBridgeableDirection<Spsc,  Direction::Producer>);
static_assert( IsBridgeableDirection<Spsc,  Direction::Consumer>);
static_assert( IsBridgeableDirection<SnapT, Direction::SwmrWriter>);
static_assert( IsBridgeableDirection<SnapT, Direction::SwmrReader>);
static_assert( IsBridgeableDirection<DequeT, Direction::Owner>);
static_assert( IsBridgeableDirection<DequeT, Direction::Thief>);

// SPSC has no SwmrWriter direction; the metafunction resolution fails.
static_assert(!IsBridgeableDirection<Spsc, Direction::SwmrWriter>);
// Snapshot has no Producer direction; the metafunction resolution fails.
static_assert(!IsBridgeableDirection<SnapT, Direction::Producer>);
// ChaseLevDeque has role-specific Owner/Thief directions only.
static_assert(!IsBridgeableDirection<DequeT, Direction::Producer>);
static_assert(!IsBridgeableDirection<DequeT, Direction::Consumer>);

// Non-substrate types are rejected by IsSubstrate gate.
static_assert(!IsBridgeableDirection<int, Direction::Producer>);

// ── VendorCtx admission for vendor-pinned payload substrates ───────

static_assert(!proto::CtxFitsPermissionedProtocol<
    default_proto_for_t<NvSpsc, Direction::Producer>,
    eff::HotFgCtx,
    proto::EmptyPermSet>);
static_assert(proto::CtxFitsPermissionedProtocol<
    default_proto_for_t<NvSpsc, Direction::Producer>,
    eff::HotFgCtx,
    proto::EmptyPermSet,
    proto::VendorCtx<proto::VendorBackend::NV>>);
static_assert(proto::CtxFitsPermissionedProtocol<
    default_proto_for_t<NvSpsc, Direction::Producer>,
    eff::HotFgCtx,
    proto::EmptyPermSet,
    proto::VendorCtx<proto::VendorBackend::Portable>>);
static_assert(!proto::CtxFitsPermissionedProtocol<
    default_proto_for_t<AmdSpsc, Direction::Consumer>,
    eff::HotFgCtx,
    proto::EmptyPermSet,
    proto::VendorCtx<proto::VendorBackend::NV>>);

using NvProducerSession = decltype(mint_substrate_session<
    NvSpsc,
    Direction::Producer,
    proto::VendorCtx<proto::VendorBackend::NV>>(
        std::declval<eff::HotFgCtx const&>(),
        std::declval<handle_for_t<NvSpsc, Direction::Producer>&>()));
static_assert(NvProducerSession::vendor_backend == proto::VendorBackend::NV);

using DequeOwnerSession = decltype(mint_substrate_session<
    DequeT,
    Direction::Owner>(
        std::declval<eff::HotFgCtx const&>(),
        std::declval<handle_for_t<DequeT, Direction::Owner>&>()));
using DequeThiefSession = decltype(mint_substrate_session<
    DequeT,
    Direction::Thief>(
        std::declval<eff::HotFgCtx const&>(),
        std::declval<handle_for_t<DequeT, Direction::Thief>&>()));
static_assert(std::is_same_v<
    typename DequeOwnerSession::protocol,
    proto::Select<proto::Send<int, proto::Continue>,
                  proto::Recv<int, proto::Continue>>>);
static_assert(std::is_same_v<
    typename DequeThiefSession::protocol,
    proto::Recv<proto::Borrowed<int, DequeT::thief_tag>, proto::Continue>>);

}  // namespace detail::substrate_session_bridge_self_test

}  // namespace crucible::concurrent
