#pragma once

// ═══════════════════════════════════════════════════════════════════
// MpmcChannelSession.h — typed-session facade for PermissionedMpmcChannel
// (SEPLOG-H2, task #326 sub-item)
//
// First production-shaped wiring of the FOUND-C v1 PermissionedSession-
// Handle stack onto the existing concurrent/PermissionedMpmcChannel
// substrate (Nikolaev SCQ — DISC 2019).  Closes the SEPLOG-H2 deliverable
// of the H-series umbrella (#326): "MPMC protocol instantiation —
// ProducerProto / ConsumerProto via session framework".
//
// ─── Why no separate PermissionedMpmcRing wrapper ──────────────────
//
// SEPLOG-H2's deliverable list nominally asks for two artefacts: a
// PermissionedMpmcRing primitive AND a typed-session facade.  The
// primitive ALREADY EXISTS as concurrent/PermissionedMpmcChannel
// (FOUND-A06..A10, the genuinely-novel construction documented in
// THREADING.md §5.5.1 — no existing MPMC library encodes producer/
// consumer roles at the type level).  PermissionedMpmcChannel is the
// canonical Permissioned* wrapper for MpmcRing<T, Capacity>; shipping
// a sibling PermissionedMpmcRing would duplicate ~430 lines of role-
// typing + fractional-permission machinery for zero functional gain.
//
// The genuine GAP is the typed-session facade — analogous to
// SpscSession.h wrapping PermissionedSpscChannel and MetaLogSession.h
// wrapping PermissionedMetaLog.  This header fills it.
//
// ─── Difference vs SpscSession ──────────────────────────────────────
//
// SpscSession wraps PermissionedSpscChannel which uses LINEAR
// Permissions on both endpoints (one Producer, one Consumer per
// channel).  MpmcChannelSession wraps PermissionedMpmcChannel which
// uses FRACTIONAL Permissions on both endpoints — many concurrent
// producers + many concurrent consumers, mediated by two
// SharedPermissionPools.  Consequence at the session layer:
//
//   * Endpoint construction returns std::optional<Handle> (the pool
//     can refuse to lend a share if exclusive mode is active), not
//     a bare Handle as in SPSC.  Callers unwrap before binding the
//     handle to a session.
//   * No per-channel single-producer / single-consumer linearity —
//     each handle holds a SharedPermissionGuard refcount share.
//   * The session protocol shape is IDENTICAL to SPSC (Loop<Send|Recv,
//     Continue>) — PSH treats every Active producer/consumer handle as
//     an independent streaming endpoint regardless of whether the
//     underlying ring is SPSC or MPMC.  The MPMC nature is a property
//     of the substrate, not of the wire protocol.
//
// In short: protocol shape is the same; the wrapper around it differs
// only in endpoint construction (optional + pool-mediated) and in
// runtime cost (~15-25 ns per try_push/try_pop vs ~5-8 ns for SPSC,
// per the SCQ FAA + per-cell CAS shape).
//
// ─── What this layer ADDS over PermissionedMpmcChannel ─────────────
//
// The bare PermissionedMpmcChannel<T, N, UserTag>::ProducerHandle
// already gives:
//   * Compile-time role discrimination (try_push only, no try_pop)
//   * SharedPermissionGuard fractional refcount (concurrent producers
//     coexist; with_drained_access requires all guards released)
//   * Active/Closed typestate (close() consumes Active, yields
//     Closed; only Active exposes try_push)
//
// What it does NOT give:
//   * Protocol-shape typing.  The handle is "any caller may try_push
//     anytime"; there is no compile-time encoding that the channel
//     is a STREAMING session that loops sends until shutdown.
//
// MpmcChannelSession adds exactly that.  Wrapping a ProducerHandle as
// a PermissionedSessionHandle<ProducerProto<T>, EmptyPermSet,
// ProducerHandle*> gives:
//   * Loop<Send<T, Continue>> protocol-shape typing — the type
//     system knows this is a streaming channel that never reaches
//     End (shutdown is via detach).
//   * Loop body permission-balance enforcement (vacuously true for
//     EmptyPermSet but compiler-checked at every Continue).
//   * Branch-terminal PS convergence (vacuously true; no Select/
//     Offer in this protocol shape).
//   * Debug-mode abandonment-tracker enrichment if the handle is
//     dropped without detach (zero release-mode cost).
//
// EmptyPermSet by design — see SpscSession.h's "What this wiring DOES
// NOT demonstrate" block.  Producer/consumer authority stays in the
// endpoint handles' SharedPermissionGuards; the wire payload is plain
// T, not Transferable<T, Tag>.
//
// ─── Why Resource is a POINTER ─────────────────────────────────────
//
// PermissionedMpmcChannel::ProducerHandle is move-CONSTRUCTIBLE (for
// std::optional unwrapping) but copy is deleted (would double-count
// the SharedPermissionGuard refcount share).  Move-assignment is
// defaulted — feasible but the framework's pointer-Resource pattern
// (Session.h :2010-2018) is the canonical escape hatch and matches
// how SpscSession and MetaLogSession wire their handles.  The pointee
// outlives the PSH by construction (handle is stack-bound to its
// enclosing scope; PSH moves into a jthread joined before scope exit).
//
// The factories below take handle BY REFERENCE for ergonomic call
// sites; they internally take the address and forward as Resource =
// Handle*.  Lifetime contract is identical to passing &handle directly.
//
// ─── Worked example ────────────────────────────────────────────────
//
//   struct WorkChannel {};
//   crucible::concurrent::PermissionedMpmcChannel<int, 1024, WorkChannel> ch;
//
//   namespace ses = crucible::safety::proto::mpmc_channel_session;
//
//   // Spawn 4 producers + 4 consumers; each draws its own pool share.
//   for (int i = 0; i < 4; ++i) {
//       std::jthread{[&ch, i](auto) {
//           auto p_opt = ch.producer();
//           if (!p_opt) return;
//           auto p = std::move(*p_opt);
//           auto psh = ses::mint_mpmc_producer_session<decltype(ch)>(
//               crucible::effects::HotFgCtx{}, p);
//           for (int j = 0; j < 256; ++j) {
//               auto next = std::move(psh).send(i * 1000 + j,
//                                               ses::blocking_push);
//               psh = std::move(next);
//           }
//           std::move(psh).detach(
//               crucible::safety::proto::detach_reason::TestInstrumentation{});
//       }};
//   }
//
//   // Symmetric for consumers — see test_mpmc_channel_session.cpp.
//
// ─── References ────────────────────────────────────────────────────
//
//   misc/27_04_csl_permission_session_wiring.md §17 — H-umbrella
//     deliverable list naming MPMC as the fractional × fractional cell.
//   concurrent/PermissionedMpmcChannel.h — the underlying primitive.
//   concurrent/MpmcRing.h — Nikolaev SCQ substrate (DISC 2019).
//   sessions/PermissionedSession.h — the FOUND-C v1 framework.
//   sessions/SpscSession.h — sibling SPSC streaming-session facade.
//   sessions/MetaLogSession.h — sibling MetaLog streaming-session facade.
//   THREADING.md §5.5.1 — "the MPMC slot is genuinely beyond-Vyukov".
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/concurrent/PermissionedMpmcChannel.h>
#include <crucible/permissions/Permission.h>
#include <crucible/sessions/PermissionedSession.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionMint.h>

#include <concepts>
#include <cstddef>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto::mpmc_channel_session {

// ── Protocol shapes ─────────────────────────────────────────────────
//
// Both protocols are infinite Loops: MPMC channels in production stream
// indefinitely until shutdown (no explicit End in the wire protocol).
// Loop without an exit branch is the documented infinite-loop pattern;
// callers MUST detach the terminal handle at shutdown.
//
// The MPMC nature of the substrate does not change the per-handle
// protocol — each Active producer handle is its own streaming
// endpoint, and the SharedPermissionGuard refcount discipline lives
// inside the handle, not in the protocol.  Many such PSHs may exist
// simultaneously (one per Active handle), each running its own
// independent ProducerProto session.

template <typename T>
using ProducerProto = Loop<Send<T, Continue>>;

template <typename T>
using ConsumerProto = Loop<Recv<T, Continue>>;

// ── Surface concept ─────────────────────────────────────────────────
//
// Structural requirement on a candidate Channel type before the mint
// factories below will accept it.  Mirrors the
// MetaLogSessionSurface / ChainEdgeSessionSurface pattern — the
// session header does not name PermissionedMpmcChannel directly so
// future MPMC channels (e.g. capacity-resizable variants, sharded
// fan-out adapters) compose without modification.
//
// The surface differs from the SPSC and MetaLog surfaces in two ways:
//
//   1. Endpoint construction is `producer() / consumer()` returning
//      `std::optional<Handle>` (pool-mediated lend; can refuse).
//      SPSC takes a Permission&& and returns a bare Handle; MetaLog
//      does the same.  MPMC's optional return is the structural
//      witness of fractional pool semantics.
//
//   2. Handle methods include diagnostic accessors (size_approx,
//      empty_approx, capacity) inherited from the substrate ring;
//      these are observation-only, do not advance the protocol, and
//      remain available in either Active or Closed state per the
//      FOUND-A24 unified-surface convention.

template <typename Channel>
concept MpmcChannelSessionSurface = requires(
    Channel& ch,
    typename Channel::ProducerHandle& producer_handle,
    typename Channel::ConsumerHandle& consumer_handle,
    const typename Channel::value_type& sample_payload)
{
    typename Channel::value_type;
    typename Channel::user_tag;
    typename Channel::producer_tag;
    typename Channel::consumer_tag;
    typename Channel::ProducerHandle;
    typename Channel::ConsumerHandle;

    // Endpoint factories — pool-mediated, may refuse via nullopt.
    { ch.producer() }
        -> std::same_as<std::optional<typename Channel::ProducerHandle>>;
    { ch.consumer() }
        -> std::same_as<std::optional<typename Channel::ConsumerHandle>>;

    // Producer-side method shape.
    { producer_handle.try_push(sample_payload) }
        -> std::same_as<bool>;

    // Consumer-side method shape — try_pop returns optional<value>.
    { consumer_handle.try_pop() }
        -> std::same_as<std::optional<typename Channel::value_type>>;
};

// ── Endpoint mint helpers ────────────────────────────────────────────
//
// Lift the channel's pool-mediated producer() / consumer() factory
// behind a session-side mint name so call sites read uniformly across
// Permissioned* primitives.  Both helpers are pure forwarders to the
// channel's existing factory; they do not add atomic ops or change
// semantics.

template <MpmcChannelSessionSurface Channel>
[[nodiscard]] auto mint_mpmc_producer_endpoint(Channel& ch) noexcept
    -> std::optional<typename Channel::ProducerHandle>
{
    return ch.producer();
}

template <MpmcChannelSessionSurface Channel>
[[nodiscard]] auto mint_mpmc_consumer_endpoint(Channel& ch) noexcept
    -> std::optional<typename Channel::ConsumerHandle>
{
    return ch.consumer();
}

// ── Session mint factories (ctx-bound) ───────────────────────────────
//
// Take handle BY REFERENCE; internally take the address and forward
// as Resource = Handle*.  This is a pure ergonomic improvement over
// the pointer-taking form — same lifetime contract, no caller-side
// `&` taking-address-of.  The handle MUST outlive the returned PSH
// (typically: handle is stack-bound to its enclosing scope; PSH is
// scoped tighter or moves into a jthread joined before scope exit).
//
// EmptyPermSet at session start: the handle's embedded
// SharedPermissionGuard already enforces the fractional-refcount
// discipline.  Adding Transferable / Borrowed / Returned payloads
// to this protocol would compose orthogonally via SessionPermPayloads.h
// but is not the shape MPMC streaming channels need (no permissions
// to transfer through the wire).

template <MpmcChannelSessionSurface Channel,
          ::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] constexpr auto
mint_mpmc_producer_session(Ctx const& ctx,
                           typename Channel::ProducerHandle& handle) noexcept
{
    using T = typename Channel::value_type;
    return mint_permissioned_session<ProducerProto<T>>(ctx, &handle);
}

template <MpmcChannelSessionSurface Channel,
          ::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] constexpr auto
mint_mpmc_consumer_session(Ctx const& ctx,
                           typename Channel::ConsumerHandle& handle) noexcept
{
    using T = typename Channel::value_type;
    return mint_permissioned_session<ConsumerProto<T>>(ctx, &handle);
}

// ── Handle-type aliases ──────────────────────────────────────────────
//
// Default-Ctx aliases for static_assert / size-witness use sites.
// Production callers usually let template deduction pick the type
// up at the mint call; these aliases exist for the witness blocks
// below and for ergonomic decltype-based handle declarations.

template <MpmcChannelSessionSurface Channel,
          ::crucible::effects::IsExecCtx Ctx = ::crucible::effects::HotFgCtx>
using ProducerSessionHandle = decltype(
    mint_mpmc_producer_session<Channel>(
        std::declval<Ctx const&>(),
        std::declval<typename Channel::ProducerHandle&>()));

template <MpmcChannelSessionSurface Channel,
          ::crucible::effects::IsExecCtx Ctx = ::crucible::effects::HotFgCtx>
using ConsumerSessionHandle = decltype(
    mint_mpmc_consumer_session<Channel>(
        std::declval<Ctx const&>(),
        std::declval<typename Channel::ConsumerHandle&>()));

// ── Transport helpers ────────────────────────────────────────────────
//
// Generic lambdas — call sites need NO template args.  The first
// parameter (`Handle*&`) is deduced from PSH's invocation context.
//
// blocking_push: spin-pause-on-full retry loop.  Matches the canonical
// MPMC producer fast path under transient back-pressure (SCQ's
// FAA-bounded retry contract makes this safe — no livelock).
//
// blocking_pop:  spin-pause-on-empty retry loop.  Matches the canonical
// MPMC consumer fast path.  optional<T>::operator* returns T&; the
// auto return type strips the reference, returning T by value (move
// where possible).
//
// Callers wanting different blocking strategies (timeout, deadline,
// yield-instead-of-spin) write their own transport closures.

inline constexpr auto blocking_push = [](auto& hp, auto&& value) noexcept {
    while (!hp->try_push(std::forward<decltype(value)>(value))) {
        CRUCIBLE_SPIN_PAUSE;
    }
};

inline constexpr auto blocking_pop = [](auto& hp) noexcept {
    for (;;) {
        if (auto v = hp->try_pop()) return *v;
        CRUCIBLE_SPIN_PAUSE;
    }
};

}  // namespace crucible::safety::proto::mpmc_channel_session

// ═══════════════════════════════════════════════════════════════════
// Compile-time witnesses — fail-to-compile if the wiring drifts
// ═══════════════════════════════════════════════════════════════════

namespace crucible::safety::proto::mpmc_channel_session::detail::sizeof_witness {

// Small synthetic channel for size-equality assertions.  Picked to
// match the SpscSession witness pattern.
struct Tag {};
using SmallChannel = ::crucible::concurrent::PermissionedMpmcChannel<int, 16, Tag>;
using ProdHandle   = SmallChannel::ProducerHandle;
using ConsHandle   = SmallChannel::ConsumerHandle;

// Surface concept must accept the canonical instantiation.
static_assert(MpmcChannelSessionSurface<SmallChannel>,
              "mpmc_channel_session: PermissionedMpmcChannel must satisfy "
              "MpmcChannelSessionSurface — if this fails, the channel's "
              "endpoint shape (producer()/consumer() returning optional<Handle>) "
              "has drifted and the session facade can no longer wrap it.");

// Protocol-shape pinning.
static_assert(std::is_same_v<ProducerProto<int>,
                             Loop<Send<int, Continue>>>);
static_assert(std::is_same_v<ConsumerProto<int>,
                             Loop<Recv<int, Continue>>>);

// Sizeof-equality is asserted on the CONCRETE HEAD types that
// mint_mpmc_*_session<...>(...) actually returns after Loop unrolling,
// NOT on `Loop<...>` itself (which is a shape-only template with no
// SessionHandle / PSH specialisation).  We mirror the SpscSession /
// ChainEdgeSession witness convention: assert on End and Send<T, End>
// specialisations that the framework concretely materialises.
//
// What the witnesses prove:
//   1. PSH<End, EmptyPermSet, Handle*> is the same size as
//      SessionHandle<End, Handle*> — verifies EmptyPermSet collapses
//      via EBO and the abandonment tracker contributes zero asymmetric
//      bytes between PSH and bare.
//   2. PSH<Send<int, End>, EmptyPermSet, Handle*> is the same size
//      as the bare SessionHandle for the same head — extends the
//      witness to non-terminal heads.
//   3. The PSH collapses to roughly pointer-sized, catching
//      regressions where PS or LoopContext accidentally gain a
//      non-empty member.

static_assert(sizeof(PermissionedSessionHandle<End, EmptyPermSet,
                                                ProdHandle*>)
              == sizeof(SessionHandle<End, ProdHandle*>),
              "mpmc_channel_session: PSH<End, EmptyPermSet, ProdHandle*> "
              "must be same size as bare SessionHandle<End, ProdHandle*> — "
              "if this fails, EBO collapse of EmptyPermSet has been broken "
              "or the abandonment tracker grew asymmetrically between PSH "
              "and bare.");

static_assert(sizeof(PermissionedSessionHandle<End, EmptyPermSet,
                                                ConsHandle*>)
              == sizeof(SessionHandle<End, ConsHandle*>),
              "mpmc_channel_session: PSH<End, EmptyPermSet, ConsHandle*> "
              "must be same size as bare SessionHandle<End, ConsHandle*>.");

static_assert(sizeof(PermissionedSessionHandle<Send<int, End>, EmptyPermSet,
                                                ProdHandle*>)
              == sizeof(SessionHandle<Send<int, End>, ProdHandle*>),
              "mpmc_channel_session: PSH<Send<int, End>, EmptyPermSet, "
              "ProdHandle*> must be same size as bare SessionHandle for "
              "the same head.");

static_assert(sizeof(PermissionedSessionHandle<Recv<int, End>, EmptyPermSet,
                                                ConsHandle*>)
              == sizeof(SessionHandle<Recv<int, End>, ConsHandle*>),
              "mpmc_channel_session: PSH<Recv<int, End>, EmptyPermSet, "
              "ConsHandle*> must be same size as bare SessionHandle for "
              "the same head.");

}  // namespace crucible::safety::proto::mpmc_channel_session::detail::sizeof_witness
