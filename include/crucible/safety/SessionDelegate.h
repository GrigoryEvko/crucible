#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::safety::proto — session-type delegation (Honda 1998
//                            throw/catch; higher-order sessions)
//
// Delegation is the mechanism by which one session hands a full
// session endpoint to another, as a first-class message payload.
// Semantically: "I currently hold my side of a T-typed channel; I
// pass THAT HANDLE to you; you now own the channel and may use it
// while I proceed with my remaining K-typed protocol."
//
// Honda 1998 (§6, "throw/catch") introduced this as the foundational
// higher-order extension to dyadic session types.  Later formulations
// (Mostrous-Yoshida 2015 HO-π, DGS12 Kobayashi encoding) show that
// delegation is representable in the linear-π base calculus but
// deserves a first-class surface because its type-level behaviour is
// distinct enough from Send/Recv to warrant its own combinator.
//
// ─── The combinator ─────────────────────────────────────────────────
//
//     Delegate<T, K>   — I send my endpoint of a T-typed channel,
//                         continue as K.
//     Accept<T, K>     — I receive an endpoint of a T-typed channel,
//                         continue as K.
//
// Duality:
//     dual(Delegate<T, K>) = Accept<T, dual(K)>
//     dual(Accept<T, K>)   = Delegate<T, dual(K)>
//
// CRITICAL: T is NOT dualised in the dual computation.  The delegated
// endpoint retains its full session type as-is; what transfers is
// the actual endpoint handle, not a copy or an inverted-role view.
// If I Delegate<T, K>, I had a SessionHandle<T, ...> and I give it
// up; my peer gains the same SessionHandle<T, ...> and may continue
// its conversation with whoever was the OTHER side of that T-session.
//
// ─── Linearity across the handoff ───────────────────────────────────
//
// Delegation IS the paradigmatic case for session-handle linearity.
// The delegator's handle is consumed; the acceptor's handle is
// produced.  Two handles of the same endpoint existing simultaneously
// would allow the original delegator and the new acceptor to both
// attempt ops on the same wire — a linearity violation.  Our
// SessionHandle's move-only + consumption-on-delegate discipline is
// exactly what Honda's system needs.
//
// ─── Cross-layer CNTP nesting (session_types.md §F.6, §II.12.7) ─────
//
// The canonical use in Crucible: CNTP Layer 1 carries Layer 2-5
// payloads.  Each upper layer's payload IS a session type.  A single
// Layer 1 channel hands out Layer 2 (SWIM), Layer 3 (Raft), Layer 4
// (collectives), or Layer 5 (NetworkOffload) endpoints via delegation:
//
//   using CntpLayer1 = Loop<Select<
//       Delegate<SwimProto,         Continue>,
//       Delegate<RaftProto,         Continue>,
//       Delegate<CollectiveProto,   Continue>,
//       Delegate<NetOffloadProto,   Continue>,
//       End
//   >>;
//
// The Layer 1 transport hands out higher-layer endpoints to the
// respective subsystems; each subsystem runs its OWN protocol on top.
// Layer 1's type gives no hint of what Layer 2-5 carry — opacity at
// the transport level, protocol integrity at the upper layers.
// Composed φ = (φ_Layer1 ∧ φ_Layer2 ∧ φ_Layer3 ∧ …) by the
// compositionality theorem (§II.12.4).
//
// ─── When NOT to use Delegate ──────────────────────────────────────
//
// - For NON-first-class values (plain data types): use Send/Recv.
//   Delegate is for session endpoints specifically.
// - For "I need a channel from the peer" where you're ASKING, not
//   HANDING OFF: use a request/response — the peer returns a channel
//   they OWN by delegating it with Delegate<T, K>.
// - For "the peer spawns a new session alongside":  that's not
//   delegation — that's fresh session establishment via
//   `establish_channel`.  Delegate transfers an EXISTING session.
//
// ─── References ────────────────────────────────────────────────────
//
//   Honda, K. (1993 & 1998).  "Types for Dyadic Interaction" +
//     "Language Primitives and Type Discipline for Structured
//     Communication-Based Programming."  Original throw/catch.
//   Mostrous, D., Yoshida, N. (2015).  "Session Typing and Asynchronous
//     Subtyping for the Higher-Order π-Calculus."  The full
//     formalisation of channel-carrying channels.
//   Dardha, O., Giachino, E., Sangiorgi, D. (2012).  "Session Types
//     Revisited."  Proves delegation is representable in linear-π
//     via Channel-carrying values.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/Session.h>

#include <concepts>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto {

// ═════════════════════════════════════════════════════════════════════
// ── Combinator types ──────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// Send my endpoint of a T-typed channel; continue as K.
template <typename T, typename K>
struct Delegate {
    using delegated_proto = T;
    using next            = K;
};

// Receive an endpoint of a T-typed channel; continue as K.
template <typename T, typename K>
struct Accept {
    using delegated_proto = T;
    using next            = K;
};

// ─── Compositional sugar ────────────────────────────────────────────
//
// Convenience aliases built on top of Delegate / Accept for common
// multi-handoff and proxy patterns.  None adds new machinery — each
// expands to nested Delegate / Accept combinators and therefore
// inherits dual, compose, well-formedness, and handle dispatch
// without needing its own specialisations.

namespace detail {

template <typename... Ts> struct delegate_seq_helper;

template <typename K>
struct delegate_seq_helper<K> { using type = K; };

template <typename Head, typename... Rest>
struct delegate_seq_helper<Head, Rest...> {
    using type = Delegate<Head, typename delegate_seq_helper<Rest...>::type>;
};

template <typename... Ts> struct accept_seq_helper;

template <typename K>
struct accept_seq_helper<K> { using type = K; };

template <typename Head, typename... Rest>
struct accept_seq_helper<Head, Rest...> {
    using type = Accept<Head, typename accept_seq_helper<Rest...>::type>;
};

}  // namespace detail

// Delegate_seq<T1, T2, ..., Tn, K> — sequential multi-handoff:
// hand off T1, then T2, ..., then Tn, then continue as K.  The LAST
// type argument is always the continuation; earlier arguments are the
// delegated session types in hand-off order.  Requires at least one
// type argument (the continuation itself; 0-arg form is ill-formed).
//
// Expands to Delegate<T1, Delegate<T2, ..., Delegate<Tn, K>>>.
//
// Duality:  dual(Delegate_seq<Ts..., K>) = Accept_seq<Ts..., dual(K)>
//           — Ts (delegated protocols) NOT dualised; K IS.
template <typename... Ts>
using Delegate_seq = typename detail::delegate_seq_helper<Ts...>::type;

// Accept_seq — peer-side companion to Delegate_seq.  A peer receiving
// the stream of delegated sessions produced by Delegate_seq<Ts..., K>
// speaks Accept_seq<Ts..., dual(K)>.  Same nesting structure, Accept
// instead of Delegate.
template <typename... Ts>
using Accept_seq = typename detail::accept_seq_helper<Ts...>::type;

// Redelegate<T, K> — middlebox / proxy pattern:  accept a T-typed
// endpoint, immediately delegate it onward on the same carrier, then
// continue as K.  The carrier briefly owns the T-session then passes
// it on; the T-session never interacts with this carrier beyond
// transport.
//
// Canonical Crucible uses:  CNTP Layer 1 → Layer 2 hand-off (Layer 1
// accepts, then delegates to the Layer 2 worker), Cipher hot-tier
// promotion to warm-tier (hot accepts the Raft-log endpoint, re-
// delegates to warm), Keeper proxying an inference session from an
// admitting peer to the serving worker.
template <typename T, typename K>
using Redelegate = Accept<T, Delegate<T, K>>;

// DelegateWithAck<T, Ack, K> — delegate T, then WAIT for a peer
// acknowledgment of type Ack, then continue as K.  Gives synchronous
// confirmation of a successful handoff: no dropped handoffs, no
// silent lost sessions, the delegator learns whether to retry.
// Useful when the handoff is expensive (large state transfer) or
// when the session protocol is critical path (a lost inference
// session should page someone, not just miss an SLA).
template <typename T, typename Ack, typename K>
using DelegateWithAck = Delegate<T, Recv<Ack, K>>;

// AcceptWithAck<T, Ack, K> — peer form: accept the delegated endpoint,
// immediately send an Ack back confirming receipt, continue as K.
template <typename T, typename Ack, typename K>
using AcceptWithAck = Accept<T, Send<Ack, K>>;

// ─── Shape traits ───────────────────────────────────────────────────

template <typename P> struct is_delegate : std::false_type {};
template <typename T, typename K>
struct is_delegate<Delegate<T, K>> : std::true_type {};

template <typename P> struct is_accept : std::false_type {};
template <typename T, typename K>
struct is_accept<Accept<T, K>> : std::true_type {};

template <typename P> inline constexpr bool is_delegate_v = is_delegate<P>::value;
template <typename P> inline constexpr bool is_accept_v   = is_accept<P>::value;

// Head-shape predicate (extends Session.h's is_head_v with delegate
// and accept — these ARE valid protocol heads, i.e., states the
// SessionHandle can be positioned at).
template <typename P>
inline constexpr bool is_delegation_head_v =
    is_delegate_v<P> || is_accept_v<P>;

// ═════════════════════════════════════════════════════════════════════
// ── Duality ────────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Delegate ↔ Accept under duality.  The delegated protocol T is NOT
// dualised — the delegated endpoint's full protocol is transferred
// as-is; the recipient owns the same endpoint the sender had.

template <typename T, typename K>
struct dual_of<Delegate<T, K>> {
    using type = Accept<T, typename dual_of<K>::type>;
};

template <typename T, typename K>
struct dual_of<Accept<T, K>> {
    using type = Delegate<T, typename dual_of<K>::type>;
};

// ═════════════════════════════════════════════════════════════════════
// ── Sequential composition ─────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// compose<Delegate<T, K>, Q> = Delegate<T, compose<K, Q>> — delegation
// is syntactically like Send/Recv in that the End to be replaced by
// Q lives within the continuation K, not the delegated T.

template <typename T, typename K, typename Q>
struct compose<Delegate<T, K>, Q> {
    using type = Delegate<T, typename compose<K, Q>::type>;
};

template <typename T, typename K, typename Q>
struct compose<Accept<T, K>, Q> {
    using type = Accept<T, typename compose<K, Q>::type>;
};

// ═════════════════════════════════════════════════════════════════════
// ── Well-formedness ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Delegate<T, K> is well-formed iff BOTH T and K are well-formed.
// T must be a complete, closed session type (no dangling Continue
// referring to an outer Loop of the delegator — delegated protocols
// are SELF-CONTAINED and may have their own independent Loops).
// K is the delegator's remaining protocol, checked in the delegator's
// LoopCtx.
//
// This is stricter than Send/Recv (which only check their continuation
// K); delegation additionally requires the delegated payload T to be
// closed-form well-formed (LoopCtx = void — T cannot reference the
// delegator's outer loop).

template <typename T, typename K, typename LoopCtx>
struct is_well_formed<Delegate<T, K>, LoopCtx>
    : std::bool_constant<
          is_well_formed<T, void>::value &&
          is_well_formed<K, LoopCtx>::value
      > {};

template <typename T, typename K, typename LoopCtx>
struct is_well_formed<Accept<T, K>, LoopCtx>
    : std::bool_constant<
          is_well_formed<T, void>::value &&
          is_well_formed<K, LoopCtx>::value
      > {};

// ═════════════════════════════════════════════════════════════════════
// ── SessionHandle specialisations ──────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// ── SessionHandle<Delegate<T, K>, Resource, LoopCtx> ───────────────
//
// Exposes ONE method: `delegate(delegated_handle, transport)`.
// Consumes both `*this` and the delegated-handle, returns a handle
// for the continuation K.  The transport is responsible for
// physically transferring the delegated endpoint's bytes / identifier
// / channel-fd to the peer; from our typing perspective it's a void-
// returning callable.

template <typename T, typename K, typename Resource, typename LoopCtx>
class [[nodiscard]] SessionHandle<Delegate<T, K>, Resource, LoopCtx>
    : public SessionHandleBase<Delegate<T, K>>
{
    Resource resource_;

    template <typename P, typename Res, typename L> friend class SessionHandle;
    template <typename P, typename Res>
    friend constexpr auto make_session_handle(Res) noexcept;
    template <typename U, typename Res, typename L>
    friend constexpr auto detail::step_to_next(Res) noexcept;

public:
    using protocol        = Delegate<T, K>;
    using delegated_proto = T;
    using continuation    = K;
    using resource_type   = Resource;
    using loop_ctx        = LoopCtx;

    constexpr explicit SessionHandle(Resource r)
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : resource_{std::move(r)} {}

    constexpr SessionHandle(SessionHandle&&) noexcept            = default;
    constexpr SessionHandle& operator=(SessionHandle&&) noexcept = default;
    ~SessionHandle()                                             = default;

    // Hand off a session endpoint of type T to the peer.  Both this
    // delegator-handle AND the delegated handle are consumed; returns
    // the continuation-K handle with resolution applied.
    //
    //   Transport signature: void(Resource&, DelegatedResource&&)
    //
    // The transport physically transfers the delegated endpoint (its
    // underlying resource bytes, channel id, fd, whatever is
    // appropriate for the transport).  The delegated handle's
    // resource is moved into the transport call; the delegated
    // handle's type-state is consumed by the && overload guarantee.
    template <typename DelegatedResource, typename DelegatedLoopCtx,
              typename Transport>
        requires std::is_invocable_v<Transport, Resource&, DelegatedResource&&>
    [[nodiscard]] constexpr auto delegate(
        SessionHandle<T, DelegatedResource, DelegatedLoopCtx>&& delegated,
        Transport transport) &&
        noexcept(std::is_nothrow_invocable_v<Transport, Resource&, DelegatedResource&&>
                 && std::is_nothrow_move_constructible_v<Resource>)
    {
        // Physically transfer the delegated endpoint.  The peer now
        // owns a SessionHandle<T, DelegatedResource, DelegatedLoopCtx>.
        std::invoke(transport, resource_, std::move(delegated.resource_));
        delegated.mark_consumed_();   // caller's delegated handle consumed
        this->mark_consumed_();
        return detail::step_to_next<K, Resource, LoopCtx>(std::move(resource_));
    }

    // Transport-less variant: consumes the delegated handle but does
    // NOT physically transfer anything.  Useful for in-memory / test /
    // stub settings where the carrier and the delegated handle live
    // in the same process and the "transfer" is merely a type-state
    // advance.  Mirrors the existing Offer::pick<I>() convenience.
    //
    // The delegated handle's resource is discarded via scope exit
    // (standard RAII).  If the Resource owns a live wire, its own
    // destructor closes it; if the user needs to extract the wire,
    // they should use the transport-taking variant instead.
    template <typename DelegatedResource, typename DelegatedLoopCtx>
    [[nodiscard]] constexpr auto delegate(
        SessionHandle<T, DelegatedResource, DelegatedLoopCtx>&& delegated) &&
        noexcept(std::is_nothrow_move_constructible_v<Resource>
                 && std::is_nothrow_destructible_v<DelegatedResource>)
    {
        // Consume the delegated handle (its resource's dtor fires
        // when this lambda scope ends; its consumed_ flag is
        // marked so the base's destructor check skips).
        delegated.mark_consumed_();
        (void)std::move(delegated);
        this->mark_consumed_();
        return detail::step_to_next<K, Resource, LoopCtx>(std::move(resource_));
    }

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return resource_; }
};

// ── SessionHandle<Accept<T, K>, Resource, LoopCtx> ─────────────────
//
// Exposes `accept(transport)`: consumes `*this`, receives the
// delegated endpoint, returns the pair (delegated-session-handle,
// continuation-K-handle).
//
//   Transport signature: DelegatedResource(Resource&)
//
// The transport returns the DelegatedResource — the underlying
// physical endpoint (bytes / id / fd / etc.) the peer just sent.
// We wrap it in a fresh SessionHandle<T, DelegatedResource, void>
// (no LoopCtx — the delegated protocol is self-contained).

template <typename T, typename K, typename Resource, typename LoopCtx>
class [[nodiscard]] SessionHandle<Accept<T, K>, Resource, LoopCtx>
    : public SessionHandleBase<Accept<T, K>>
{
    Resource resource_;

    template <typename P, typename Res, typename L> friend class SessionHandle;
    template <typename P, typename Res>
    friend constexpr auto make_session_handle(Res) noexcept;
    template <typename U, typename Res, typename L>
    friend constexpr auto detail::step_to_next(Res) noexcept;

public:
    using protocol        = Accept<T, K>;
    using delegated_proto = T;
    using continuation    = K;
    using resource_type   = Resource;
    using loop_ctx        = LoopCtx;

    constexpr explicit SessionHandle(Resource r)
        noexcept(std::is_nothrow_move_constructible_v<Resource>)
        : resource_{std::move(r)} {}

    constexpr SessionHandle(SessionHandle&&) noexcept            = default;
    constexpr SessionHandle& operator=(SessionHandle&&) noexcept = default;
    ~SessionHandle()                                             = default;

    // Receive the delegated endpoint + advance past Accept.  Returns
    // (delegated_handle, continuation_handle).  The DelegatedResource
    // type is deduced from the Transport's return type.
    template <typename Transport,
              typename DelegatedResource = std::invoke_result_t<Transport, Resource&>>
        requires std::is_invocable_v<Transport, Resource&>
    [[nodiscard]] constexpr auto accept(Transport transport) &&
        noexcept(std::is_nothrow_invocable_v<Transport, Resource&>
                 && std::is_nothrow_move_constructible_v<Resource>
                 && std::is_nothrow_move_constructible_v<DelegatedResource>)
    {
        DelegatedResource delegated_res = std::invoke(transport, resource_);
        this->mark_consumed_();
        auto delegated_handle = make_session_handle<T>(std::move(delegated_res));
        auto continuation_handle =
            detail::step_to_next<K, Resource, LoopCtx>(std::move(resource_));
        return std::pair{std::move(delegated_handle),
                         std::move(continuation_handle)};
    }

    // Transport-less variant: caller provides the DelegatedResource
    // directly (either because it was handed to them via an out-of-
    // band channel, or because they're running an in-memory / test
    // transport where the "receive" is really just a type-state
    // advance).  Mirrors delegate()'s transport-less form.
    template <typename DelegatedResource>
    [[nodiscard]] constexpr auto accept_with(DelegatedResource delegated_res) &&
        noexcept(std::is_nothrow_move_constructible_v<Resource>
                 && std::is_nothrow_move_constructible_v<DelegatedResource>)
    {
        this->mark_consumed_();
        auto delegated_handle = make_session_handle<T>(std::move(delegated_res));
        auto continuation_handle =
            detail::step_to_next<K, Resource, LoopCtx>(std::move(resource_));
        return std::pair{std::move(delegated_handle),
                         std::move(continuation_handle)};
    }

    [[nodiscard]] constexpr Resource&       resource() &        noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const &  noexcept { return resource_; }
};

// ═════════════════════════════════════════════════════════════════════
// ── Ergonomic surface ──────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// Concept: is CarrierProto's head a Delegate/Accept of a T-typed
// session?  Use at boundary functions that demand a specific
// delegation contract.
template <typename CarrierProto, typename DelegatedProto>
concept DelegatesTo =
    is_delegate_v<CarrierProto>
    && std::is_same_v<typename CarrierProto::delegated_proto, DelegatedProto>;

template <typename CarrierProto, typename DelegatedProto>
concept AcceptsFrom =
    is_accept_v<CarrierProto>
    && std::is_same_v<typename CarrierProto::delegated_proto, DelegatedProto>;

// Concept describing a callable usable as the Transport argument to
// SessionHandle<Delegate<T, K>, Resource>::delegate(delegated, transport).
// Required signature:
//     void transport(CarrierRes&, DelegatedRes&&)
//
// Use at boundary APIs that accept a user-supplied transport so the
// diagnostic fires at the call site naming Transport / CarrierRes /
// DelegatedRes rather than deep in delegate()'s template instantiation:
//
//     template <typename T>
//     void handoff_via(
//         SessionHandle<Delegate<T, End>, MyCarrier>&& h,
//         SessionHandle<T,               MyDelegated>&& d,
//         TransportForDelegate<MyCarrier, MyDelegated> auto transport);
//
// Zero runtime cost; composable with the other session-type concepts.
template <typename Transport, typename CarrierRes, typename DelegatedRes>
concept TransportForDelegate =
    std::is_invocable_v<Transport, CarrierRes&, DelegatedRes&&>;

// Concept describing a callable usable as the Transport argument to
// SessionHandle<Accept<T, K>, Resource>::accept(transport).  Required:
//     DelegatedRes transport(CarrierRes&)
//
// Checks BOTH invocability AND return-type agreement.  A transport
// that returns the wrong type is rejected at the call site rather than
// silently changing the deduced DelegatedResource of accept() through
// template argument deduction on the default template parameter.
template <typename Transport, typename CarrierRes, typename DelegatedRes>
concept TransportForAccept =
    std::is_invocable_v<Transport, CarrierRes&>
    && std::is_same_v<std::invoke_result_t<Transport, CarrierRes&>,
                       DelegatedRes>;

// Assertion helpers — one-liner at call sites that demand a specific
// delegation contract.  Emits the diagnostic right at the assert site
// instead of deep in a template instantiation.
template <typename CarrierProto, typename DelegatedProto>
consteval void assert_delegates_to() noexcept {
    static_assert(DelegatesTo<CarrierProto, DelegatedProto>,
        "crucible::session::diagnostic [ProtocolViolation_State]: "
        "assert_delegates_to: CarrierProto must be "
        "Delegate<DelegatedProto, K> for some K.  Check the template-"
        "instantiation context for the actual CarrierProto and "
        "DelegatedProto types; common mismatches are (a) CarrierProto "
        "starts with Send/Recv/Select/Offer instead of Delegate, or "
        "(b) the delegated_proto nested alias does not equal the "
        "requested DelegatedProto.");
}

template <typename CarrierProto, typename DelegatedProto>
consteval void assert_accepts_from() noexcept {
    static_assert(AcceptsFrom<CarrierProto, DelegatedProto>,
        "crucible::session::diagnostic [ProtocolViolation_State]: "
        "assert_accepts_from: CarrierProto must be "
        "Accept<DelegatedProto, K> for some K.");
}

// ═════════════════════════════════════════════════════════════════════
// ── Framework self-test static_asserts ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Verify the Delegate/Accept combinator set is internally consistent
// under duality, composition, and well-formedness.  Catches regressions
// to this header or to Session.h's primary templates.

namespace detail::delegate_self_test {

// Small placeholder session used as the delegated protocol.
struct Req {};
struct Ack {};
using DelegatedProto = Send<Req, Recv<Ack, End>>;

// ── Duality ────────────────────────────────────────────────────────

// dual(Delegate<T, End>) = Accept<T, End>
static_assert(std::is_same_v<
    dual_of_t<Delegate<DelegatedProto, End>>,
    Accept<DelegatedProto, End>>);

// dual(Accept<T, End>) = Delegate<T, End>
static_assert(std::is_same_v<
    dual_of_t<Accept<DelegatedProto, End>>,
    Delegate<DelegatedProto, End>>);

// Involution: dual(dual(Delegate<T, K>)) == Delegate<T, K>
static_assert(std::is_same_v<
    dual_of_t<dual_of_t<Delegate<DelegatedProto, Send<int, End>>>>,
    Delegate<DelegatedProto, Send<int, End>>>);

// T is NOT dualised — critical invariant: the delegated endpoint's
// protocol stays as-is through the dual operation.
using DelegatedAsymmetric = Send<Req, End>;  // dual would be Recv<Req, End>
static_assert(std::is_same_v<
    dual_of_t<Delegate<DelegatedAsymmetric, End>>,
    Accept<DelegatedAsymmetric, End>>);
// Note the Accept still references DelegatedAsymmetric (not its dual).

// Dual interacts with continuation dual (the K side flips, T does not).
static_assert(std::is_same_v<
    dual_of_t<Delegate<DelegatedProto, Send<int, End>>>,
    Accept<DelegatedProto, Recv<int, End>>>);

// ── Composition ────────────────────────────────────────────────────

// compose<Delegate<T, End>, Q> = Delegate<T, Q>
static_assert(std::is_same_v<
    compose_t<Delegate<DelegatedProto, End>, Send<int, End>>,
    Delegate<DelegatedProto, Send<int, End>>>);

// compose<Delegate<T, Send<int, End>>, Recv<bool, End>>
//   = Delegate<T, Send<int, Recv<bool, End>>>
static_assert(std::is_same_v<
    compose_t<Delegate<DelegatedProto, Send<int, End>>, Recv<bool, End>>,
    Delegate<DelegatedProto, Send<int, Recv<bool, End>>>>);

// Composition leaves T untouched (same semantics as Send/Recv — the
// composition-Q only flows into the continuation).
static_assert(std::is_same_v<
    compose_t<Accept<DelegatedAsymmetric, End>, End>,
    Accept<DelegatedAsymmetric, End>>);

// ── Well-formedness ────────────────────────────────────────────────

// Both Delegate and Accept are WF when T and K are WF.
static_assert(is_well_formed_v<Delegate<DelegatedProto, End>>);
static_assert(is_well_formed_v<Accept<DelegatedProto, End>>);

// Nested delegation: Delegate<Delegate<A, End>, End> is well-formed.
static_assert(is_well_formed_v<
    Delegate<Delegate<Send<Req, End>, End>, End>>);

// T WF but K has a free Continue (no enclosing Loop) → ill-formed.
static_assert(!is_well_formed_v<Delegate<DelegatedProto, Continue>>);

// K WF but T has a free Continue (checked in T's own LoopCtx=void).
// Since T's well-formedness is checked with void LoopCtx, a free
// Continue in T makes the whole Delegate ill-formed.
static_assert(!is_well_formed_v<Delegate<Continue, End>>);

// Delegate inside a Loop is fine (the Loop gives Continue a binding).
static_assert(is_well_formed_v<
    Loop<Delegate<DelegatedProto, Continue>>>);

// ── Shape predicates ───────────────────────────────────────────────

static_assert(is_delegate_v<Delegate<DelegatedProto, End>>);
static_assert(!is_delegate_v<Accept<DelegatedProto, End>>);
static_assert(!is_delegate_v<Send<int, End>>);

static_assert(is_accept_v<Accept<DelegatedProto, End>>);
static_assert(!is_accept_v<Delegate<DelegatedProto, End>>);

static_assert(is_delegation_head_v<Delegate<DelegatedProto, End>>);
static_assert(is_delegation_head_v<Accept<DelegatedProto, End>>);
static_assert(!is_delegation_head_v<Send<int, End>>);

// ── Canonical CNTP cross-layer example (shape only) ───────────────
//
// Session_types.md §II.12.7 describes CNTP Layer 1 carrying higher-
// layer sessions via delegation.  Verify the type-level shape:

namespace cntp_cross_layer_example {
    // Upper-layer protocols (opaque to Layer 1)
    struct SwimProbe {};
    struct SwimAck   {};
    using SwimProto = Loop<Send<SwimProbe, Recv<SwimAck, Continue>>>;

    struct RaftAppend {};
    struct RaftAck    {};
    using RaftProto = Loop<Recv<RaftAppend, Send<RaftAck, Continue>>>;

    // Layer 1's protocol: a loop that either delegates a SWIM endpoint
    // OR delegates a Raft endpoint OR terminates.
    using CntpLayer1 = Loop<Select<
        Delegate<SwimProto, Continue>,
        Delegate<RaftProto, Continue>,
        End
    >>;

    static_assert(is_well_formed_v<CntpLayer1>);

    // Dual is Offer<Accept<…>, Accept<…>, End> — the peer OFFERS to
    // accept whatever session the sender delegates.
    using CntpLayer1Peer = dual_of_t<CntpLayer1>;
    static_assert(std::is_same_v<CntpLayer1Peer,
        Loop<Offer<
            Accept<SwimProto, Continue>,
            Accept<RaftProto, Continue>,
            End
        >>>);
    static_assert(is_well_formed_v<CntpLayer1Peer>);

    // Involution holds through the nested delegation structure.
    static_assert(std::is_same_v<dual_of_t<CntpLayer1Peer>, CntpLayer1>);
}

// ── Loop<Delegate<…>> duality ──────────────────────────────────────
//
// Dualisation must commute with Loop:
//     dual(Loop<Delegate<T, Continue>>) = Loop<dual(Delegate<T, Continue>)>
//                                        = Loop<Accept<T, Continue>>
//
// The T stays un-dualised even under Loop; regression test.

using LoopedDelegator = Loop<Delegate<DelegatedProto, Continue>>;
using LoopedAcceptor  = Loop<Accept<DelegatedProto,  Continue>>;

static_assert(std::is_same_v<dual_of_t<LoopedDelegator>, LoopedAcceptor>);
static_assert(std::is_same_v<dual_of_t<LoopedAcceptor>,  LoopedDelegator>);
static_assert(std::is_same_v<dual_of_t<dual_of_t<LoopedDelegator>>,
                              LoopedDelegator>);  // involution under Loop
static_assert(is_well_formed_v<LoopedDelegator>);
static_assert(is_well_formed_v<LoopedAcceptor>);

// ── Concept / assert-helper compile test ───────────────────────────
static_assert(DelegatesTo<Delegate<DelegatedProto, End>, DelegatedProto>);
static_assert(!DelegatesTo<Delegate<DelegatedProto, End>, Send<int, End>>);  // wrong T
static_assert(!DelegatesTo<Accept<DelegatedProto, End>, DelegatedProto>);    // wrong head
static_assert(AcceptsFrom<Accept<DelegatedProto, End>, DelegatedProto>);

consteval bool check_assert_delegates() {
    assert_delegates_to<Delegate<DelegatedProto, End>, DelegatedProto>();
    assert_accepts_from<Accept<DelegatedProto, End>,   DelegatedProto>();
    return true;
}
static_assert(check_assert_delegates());

// ── Delegate_seq / Accept_seq ──────────────────────────────────────

// Single-argument form is the identity — the bare continuation.
static_assert(std::is_same_v<Delegate_seq<End>, End>);
static_assert(std::is_same_v<Accept_seq<End>,   End>);

// Two-argument form expands to exactly one Delegate / Accept wrap.
static_assert(std::is_same_v<
    Delegate_seq<DelegatedProto, End>,
    Delegate<DelegatedProto, End>>);
static_assert(std::is_same_v<
    Accept_seq<DelegatedProto, End>,
    Accept<DelegatedProto, End>>);

// Three-argument form — two hand-offs then the continuation.
static_assert(std::is_same_v<
    Delegate_seq<Send<Req, End>, Recv<Ack, End>, End>,
    Delegate<Send<Req, End>, Delegate<Recv<Ack, End>, End>>>);

// Four-argument form — three hand-offs then the continuation.
static_assert(std::is_same_v<
    Delegate_seq<DelegatedProto, DelegatedProto, DelegatedProto, End>,
    Delegate<DelegatedProto,
        Delegate<DelegatedProto,
            Delegate<DelegatedProto, End>>>>);

// Duality:  dual(Delegate_seq<Ts..., K>) = Accept_seq<Ts..., dual(K)>.
// Ts (delegated protocols) NOT dualised; final continuation K IS.
static_assert(std::is_same_v<
    dual_of_t<Delegate_seq<DelegatedProto, DelegatedProto, End>>,
    Accept_seq<DelegatedProto, DelegatedProto, End>>);
static_assert(std::is_same_v<
    dual_of_t<Delegate_seq<DelegatedProto, Send<int, End>>>,
    Accept_seq<DelegatedProto, Recv<int, End>>>);

// Involution under dual:  dual(dual(Delegate_seq<...>)) == original.
static_assert(std::is_same_v<
    dual_of_t<dual_of_t<Delegate_seq<DelegatedProto, DelegatedProto, End>>>,
    Delegate_seq<DelegatedProto, DelegatedProto, End>>);

// Well-formedness propagates from each component.
static_assert(is_well_formed_v<
    Delegate_seq<DelegatedProto, DelegatedProto, End>>);
static_assert(is_well_formed_v<
    Accept_seq<DelegatedProto, DelegatedProto, End>>);

// ── Redelegate ─────────────────────────────────────────────────────

// Structural expansion:  accept then delegate on the same carrier.
static_assert(std::is_same_v<
    Redelegate<DelegatedProto, End>,
    Accept<DelegatedProto, Delegate<DelegatedProto, End>>>);

// Dual flips each combinator in place; T stays un-dualised.
static_assert(std::is_same_v<
    dual_of_t<Redelegate<DelegatedProto, End>>,
    Delegate<DelegatedProto, Accept<DelegatedProto, End>>>);

// Involution.
static_assert(std::is_same_v<
    dual_of_t<dual_of_t<Redelegate<DelegatedProto, Send<int, End>>>>,
    Redelegate<DelegatedProto, Send<int, End>>>);

// Well-formed when T and K are.
static_assert(is_well_formed_v<Redelegate<DelegatedProto, End>>);

// ── DelegateWithAck / AcceptWithAck ────────────────────────────────

struct AckFixture {};

// Structural expansion.
static_assert(std::is_same_v<
    DelegateWithAck<DelegatedProto, AckFixture, End>,
    Delegate<DelegatedProto, Recv<AckFixture, End>>>);

static_assert(std::is_same_v<
    AcceptWithAck<DelegatedProto, AckFixture, End>,
    Accept<DelegatedProto, Send<AckFixture, End>>>);

// Duality:  DelegateWithAck ↔ AcceptWithAck.  T preserved; Recv/Send
// flipped; tail End unchanged.
static_assert(std::is_same_v<
    dual_of_t<DelegateWithAck<DelegatedProto, AckFixture, End>>,
    AcceptWithAck<DelegatedProto, AckFixture, End>>);

// Involution.
static_assert(std::is_same_v<
    dual_of_t<dual_of_t<DelegateWithAck<DelegatedProto, AckFixture, End>>>,
    DelegateWithAck<DelegatedProto, AckFixture, End>>);

// Well-formed.
static_assert(is_well_formed_v<
    DelegateWithAck<DelegatedProto, AckFixture, End>>);
static_assert(is_well_formed_v<
    AcceptWithAck<DelegatedProto, AckFixture, End>>);

// ── TransportForDelegate / TransportForAccept concepts ─────────────
//
// Exercise the concepts using function-pointer types (not lambdas) —
// stable at namespace scope and side-steps the -Werror=noexcept
// warning that lambdas trigger when their body might throw.

namespace transport_concept_test {
    struct CarrierRes   {};
    struct DelegatedRes {};

    using ValidDelegateTransport = void (*)(CarrierRes&, DelegatedRes&&);
    using ValidAcceptTransport   = DelegatedRes (*)(CarrierRes&);
    using WrongArityTransport    = void (*)(int, int, int);
    using WrongReturnTransport   = int  (*)(CarrierRes&);

    // TransportForDelegate: requires 2-arg signature (CarrierRes&, DelegatedRes&&)
    static_assert( TransportForDelegate<ValidDelegateTransport, CarrierRes, DelegatedRes>);
    static_assert(!TransportForDelegate<ValidAcceptTransport,   CarrierRes, DelegatedRes>);
    static_assert(!TransportForDelegate<WrongArityTransport,    CarrierRes, DelegatedRes>);

    // TransportForAccept: requires 1-arg signature + exact return type
    static_assert( TransportForAccept<ValidAcceptTransport,    CarrierRes, DelegatedRes>);
    static_assert(!TransportForAccept<ValidDelegateTransport,  CarrierRes, DelegatedRes>);
    static_assert(!TransportForAccept<WrongReturnTransport,    CarrierRes, DelegatedRes>);
}

}  // namespace detail::delegate_self_test

}  // namespace crucible::safety::proto
