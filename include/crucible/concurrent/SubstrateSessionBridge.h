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
//     mint_substrate_session<Substr, Dir>(ctx, channel, perm)
//                                                 → PSH<default_proto, ...>
//
// ── What this header ships ──────────────────────────────────────────
//
//   Direction                — enum tag for the per-substrate role
//                              (Producer / Consumer / SwmrWriter /
//                              SwmrReader; ChaseLevDeque deferred to
//                              v2 because its protocol is more complex
//                              than a plain Loop<Send/Recv>).
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
//   mint_substrate_session<Substr, Dir>(ctx, channel, perm)
//                            — factory.  Validates SubstrateFitsCtx
//                              Residency at construction; returns the
//                              standard PermissionedSessionHandle from
//                              `sessions/PermissionedSession.h` typed
//                              over `default_proto_for_t<Substr, Dir>`
//                              with EmptyPermSet (the substrate's
//                              Permission discipline already enforces
//                              single-producer-or-multi-producer
//                              linearity at the handle layer).
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
// v1 covers: SPSC, MPSC, MPMC, Snapshot.
// v2 deferred: ChaseLevDeque (owner can both push AND pop — protocol
//              is `Loop<Select<Send<T, Continue>, Recv<T, Continue>>>`
//              or split into two endpoints; needs a design call).
// v2 deferred: ShardedGrid / CalendarGrid (multi-shard substrates
//              need per-shard mint factories).

#include <crucible/concurrent/PermissionedMpmcChannel.h>
#include <crucible/concurrent/PermissionedMpscChannel.h>
#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/concurrent/Substrate.h>
#include <crucible/concurrent/SubstrateCtxFit.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/sessions/PermissionedSession.h>
#include <crucible/sessions/Session.h>

#include <cstdint>
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
    // Owner / Thief reserved for ChaseLevDeque (v2).
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

// ── mint_substrate_session<Substr, Dir>(ctx, handle) ───────────────
//
// The Universal Mint factory.  Validates:
//   * Substrate ↔ Ctx residency fit (SubstrateFitsCtxResidency)
//   * Substrate × Direction is bridgeable (has handle_for and
//     default_proto_for specializations)
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

template <class Substr, Direction Dir, ::crucible::effects::IsExecCtx Ctx>
    requires IsBridgeableDirection<Substr, Dir>
          && SubstrateFitsCtxResidency<Substr, Ctx>
[[nodiscard]] constexpr auto
mint_substrate_session(Ctx const&, handle_for_t<Substr, Dir>& handle) noexcept
{
    using Proto = default_proto_for_t<Substr, Dir>;
    using Handle = handle_for_t<Substr, Dir>;
    return ::crucible::safety::proto::mint_permissioned_session<Proto, Handle*>(&handle);
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

// ── IsBridgeableDirection concept ──────────────────────────────────

static_assert( IsBridgeableDirection<Spsc,  Direction::Producer>);
static_assert( IsBridgeableDirection<Spsc,  Direction::Consumer>);
static_assert( IsBridgeableDirection<SnapT, Direction::SwmrWriter>);
static_assert( IsBridgeableDirection<SnapT, Direction::SwmrReader>);

// SPSC has no SwmrWriter direction; the metafunction resolution fails.
static_assert(!IsBridgeableDirection<Spsc, Direction::SwmrWriter>);
// Snapshot has no Producer direction; the metafunction resolution fails.
static_assert(!IsBridgeableDirection<SnapT, Direction::Producer>);

// Non-substrate types are rejected by IsSubstrate gate.
static_assert(!IsBridgeableDirection<int, Direction::Producer>);

}  // namespace detail::substrate_session_bridge_self_test

// ── Runtime smoke test ──────────────────────────────────────────────
//
// Constructs a real channel + permission split + handle, then mints a
// substrate session.  Verifies the returned PSH has the expected
// type signature.  Uses HotFgCtx (L1Resident, Row<>) which fits the
// 64-element SPSC at 256 bytes.

[[gnu::cold]] inline void runtime_smoke_test_substrate_session_bridge() noexcept {
    namespace eff   = ::crucible::effects;
    namespace proto = ::crucible::safety::proto;
    namespace saf   = ::crucible::safety;

    struct SmokeTag {};
    using Channel = PermissionedSpscChannel<int, 64, SmokeTag>;

    Channel ch;
    auto whole = saf::mint_permission_root<spsc_tag::Whole<SmokeTag>>();
    auto [pp, cp] = saf::mint_permission_split<
        spsc_tag::Producer<SmokeTag>, spsc_tag::Consumer<SmokeTag>>(std::move(whole));

    auto prod = ch.producer(std::move(pp));
    auto cons = ch.consumer(std::move(cp));

    eff::HotFgCtx fg;
    [[maybe_unused]] auto prod_session =
        mint_substrate_session<Channel, Direction::Producer>(fg, prod);
    [[maybe_unused]] auto cons_session =
        mint_substrate_session<Channel, Direction::Consumer>(fg, cons);

    // Type-level pinning of the returned handle's protocol.
    using ProdProto = decltype(prod_session)::protocol;
    static_assert(std::is_same_v<ProdProto, proto::Send<int, proto::Continue>>,
        "mint_substrate_session<Spsc, Producer> should unroll Loop into Send<int, Continue>");

    using ConsProto = decltype(cons_session)::protocol;
    static_assert(std::is_same_v<ConsProto, proto::Recv<int, proto::Continue>>,
        "mint_substrate_session<Spsc, Consumer> should unroll Loop into Recv<int, Continue>");

    // Detach to satisfy SessionHandleBase abandonment check.
    std::move(prod_session).detach(proto::detach_reason::TestInstrumentation{});
    std::move(cons_session).detach(proto::detach_reason::TestInstrumentation{});

    // Reclaim the recombined whole permission so the channel can shut down.
    [[maybe_unused]] auto recombined = saf::mint_permission_combine<
        spsc_tag::Whole<SmokeTag>>(
            saf::mint_permission_root<spsc_tag::Producer<SmokeTag>>(),
            saf::mint_permission_root<spsc_tag::Consumer<SmokeTag>>());
}

}  // namespace crucible::concurrent
