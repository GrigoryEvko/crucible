#pragma once

// Delegation hands a whole session endpoint to a peer as a message payload.
// The delegator holds its side of a T-typed channel, passes that handle on,
// and proceeds with its own remaining protocol.
//
// The delegated protocol T is not dualized anywhere in this header.  What
// crosses the wire is the endpoint itself, not a copy and not the peer's view
// of it: the recipient ends up holding the very handle the delegator held, and
// keeps talking to whoever is on the far side of that T-session.  Dualizing T
// would describe the far side instead, which is a different conversation.
//
// The delegator's handle is consumed and the recipient's is produced, never
// both at once.  Two live handles on one endpoint would let either side act on
// the same wire.
//
// The epoch-carrying forms add a freshness threshold to the same ownership
// transfer.  The threshold is a compile-time admission fact on the recipient's
// context and has no runtime representation here.

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionEventLog.h>

#include <concepts>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::safety::proto {

template <typename T, typename K>
struct Delegate {
    using delegated_proto = T;
    using next = K;
};

template <typename T, typename K>
struct Accept {
    using delegated_proto = T;
    using next = K;
};

// The threshold declared here binds the peer's matching accept, not this
// sender.  A sender must be running at exactly the declared coordinates, so it
// cannot offer a handoff weaker than its own position.
template <typename T, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
struct EpochedDelegate {
    using delegated_proto = T;
    using next = K;
    static constexpr std::uint64_t min_epoch = MinEpoch;
    static constexpr std::uint64_t min_generation = MinGeneration;
};

// A recipient at or above the threshold may accept, so a context that has
// already advanced still qualifies.
template <typename T, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
struct EpochedAccept {
    using delegated_proto = T;
    using next = K;
    static constexpr std::uint64_t min_epoch = MinEpoch;
    static constexpr std::uint64_t min_generation = MinGeneration;
};

}  // namespace crucible::safety::proto

// A handoff creates an obligation on the delegator, not only on the recipient.
// If the recipient dies holding the delegated endpoint, the delegator is
// already past the handoff and running its own continuation.  The classifier
// below decides whether that continuation can survive it.  Either the
// delegated protocol can be abandoned cleanly where it stands, or the
// continuation has to offer a branch that receives the recipient's crash.
// Without this, "the recipient's problem" becomes a hole in the proof.
//
// The three answers are a recovery continuation, an abort, and a shape whose
// crash semantics this layer does not define.  A recipient that can only
// receive is abandonable, so the delegator's own continuation is the recovery.
// A recipient that can emit, choose, or delegate onward is not, so its crash
// has to be visible to the delegator.
//
// Recursion is bounded by protocol depth.  Each loop body is inspected once
// and the loop edge terminates the walk.

namespace crucible::safety::proto {

template <typename RecoveryProto>
struct Recovers {
    using recovery_proto = RecoveryProto;
};

struct MustAbort {};
struct IllFormed {};

namespace detail::delegate_crash {

template <typename Result>
struct propagation_is_recoverable : std::false_type {};

template <typename RecoveryProto>
struct propagation_is_recoverable<Recovers<RecoveryProto>> : std::true_type {};

template <typename Result>
inline constexpr bool propagation_is_recoverable_v = propagation_is_recoverable<Result>::value;

template <typename Branch, typename RecipientTag>
struct crash_recovery_branch : std::false_type {};

template <typename RecipientTag, typename RecoveryProto>
struct crash_recovery_branch<Recv<Crash<RecipientTag>, RecoveryProto>, RecipientTag> : std::true_type {
    using recovery_proto = RecoveryProto;
};

template <typename RecipientTag, typename... Branches>
struct first_crash_recovery;

template <typename RecipientTag>
struct first_crash_recovery<RecipientTag> {
    using type = MustAbort;
};

template <bool IsRecovery, typename RecipientTag, typename Head, typename... Tail>
struct first_crash_recovery_step;

template <typename RecipientTag, typename Head, typename... Tail>
struct first_crash_recovery_step<true, RecipientTag, Head, Tail...> {
    using type = Recovers<typename crash_recovery_branch<Head, RecipientTag>::recovery_proto>;
};

template <typename RecipientTag, typename Head, typename... Tail>
struct first_crash_recovery_step<false, RecipientTag, Head, Tail...> : first_crash_recovery<RecipientTag, Tail...> {};

template <typename RecipientTag, typename Head, typename... Tail>
struct first_crash_recovery<RecipientTag, Head, Tail...>
    : first_crash_recovery_step<crash_recovery_branch<Head, RecipientTag>::value, RecipientTag, Head, Tail...> {};

template <typename CarrierK, typename RecipientTag>
struct carrier_crash_recovery : std::type_identity<MustAbort> {};

template <typename RecipientTag, typename... Branches>
struct carrier_crash_recovery<Offer<Branches...>, RecipientTag> : first_crash_recovery<RecipientTag, Branches...> {};

template <typename Role, typename RecipientTag, typename... Branches>
struct carrier_crash_recovery<Offer<Sender<Role>, Branches...>, RecipientTag>
    : first_crash_recovery<RecipientTag, Branches...> {};

template <typename Body, typename RecipientTag>
struct carrier_crash_recovery<Loop<Body>, RecipientTag> : carrier_crash_recovery<Body, RecipientTag> {};

template <typename T, typename RecipientTag, typename CarrierK>
struct delegated_crash_propagation_impl : std::type_identity<IllFormed> {};

template <typename RecipientTag, typename CarrierK>
struct delegated_crash_propagation_impl<End, RecipientTag, CarrierK> : std::type_identity<Recovers<CarrierK>> {};

template <CrashClass C, typename RecipientTag, typename CarrierK>
struct delegated_crash_propagation_impl<Stop_g<C>, RecipientTag, CarrierK> : std::type_identity<IllFormed> {};

template <typename RecipientTag, typename CarrierK>
struct delegated_crash_propagation_impl<Continue, RecipientTag, CarrierK> : std::type_identity<Recovers<CarrierK>> {};

template <typename Msg, typename Next, typename RecipientTag, typename CarrierK>
struct delegated_crash_propagation_impl<Recv<Msg, Next>, RecipientTag, CarrierK>
    : delegated_crash_propagation_impl<Next, RecipientTag, CarrierK> {};

template <typename Msg, typename Next, typename RecipientTag, typename CarrierK>
struct delegated_crash_propagation_impl<Send<Msg, Next>, RecipientTag, CarrierK>
    : carrier_crash_recovery<CarrierK, RecipientTag> {};

template <typename... Branches, typename RecipientTag, typename CarrierK>
struct delegated_crash_propagation_impl<Offer<Branches...>, RecipientTag, CarrierK> {
    using type = std::conditional_t<
        (propagation_is_recoverable_v<typename delegated_crash_propagation_impl<Branches, RecipientTag, CarrierK>::type>
         && ...),
        Recovers<CarrierK>, typename carrier_crash_recovery<CarrierK, RecipientTag>::type>;
};

template <typename Role, typename... Branches, typename RecipientTag, typename CarrierK>
struct delegated_crash_propagation_impl<Offer<Sender<Role>, Branches...>, RecipientTag, CarrierK> {
    using type = std::conditional_t<
        (propagation_is_recoverable_v<typename delegated_crash_propagation_impl<Branches, RecipientTag, CarrierK>::type>
         && ...),
        Recovers<CarrierK>, typename carrier_crash_recovery<CarrierK, RecipientTag>::type>;
};

template <typename... Branches, typename RecipientTag, typename CarrierK>
struct delegated_crash_propagation_impl<Select<Branches...>, RecipientTag, CarrierK>
    : carrier_crash_recovery<CarrierK, RecipientTag> {};

template <typename Body, typename RecipientTag, typename CarrierK>
struct delegated_crash_propagation_impl<Loop<Body>, RecipientTag, CarrierK>
    : delegated_crash_propagation_impl<Body, RecipientTag, CarrierK> {};

template <typename T, typename K, typename RecipientTag, typename CarrierK>
struct delegated_crash_propagation_impl<Delegate<T, K>, RecipientTag, CarrierK>
    : carrier_crash_recovery<CarrierK, RecipientTag> {};

template <typename T, typename K, typename RecipientTag, typename CarrierK>
struct delegated_crash_propagation_impl<Accept<T, K>, RecipientTag, CarrierK>
    : delegated_crash_propagation_impl<K, RecipientTag, CarrierK> {};

template <typename T, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration, typename RecipientTag,
          typename CarrierK>
struct delegated_crash_propagation_impl<EpochedDelegate<T, K, MinEpoch, MinGeneration>, RecipientTag, CarrierK>
    : carrier_crash_recovery<CarrierK, RecipientTag> {};

template <typename T, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration, typename RecipientTag,
          typename CarrierK>
struct delegated_crash_propagation_impl<EpochedAccept<T, K, MinEpoch, MinGeneration>, RecipientTag, CarrierK>
    : delegated_crash_propagation_impl<K, RecipientTag, CarrierK> {};

}  // namespace detail::delegate_crash

template <typename T, typename RecipientTag, typename CarrierK>
struct delegated_crash_propagation
    : detail::delegate_crash::delegated_crash_propagation_impl<T, RecipientTag, CarrierK> {};

template <typename T, typename RecipientTag, typename CarrierK>
using delegated_crash_propagation_t = typename delegated_crash_propagation<T, RecipientTag, CarrierK>::type;

namespace detail::delegate_crash {

template <typename>
inline constexpr bool dependent_false_v = false;

template <typename Result>
struct propagation_assertion {
    template <typename Actual = Result>
    static consteval void check() noexcept {
        static_assert(dependent_false_v<Actual>, "crucible::session::diagnostic "
                                                 "[DelegatedCrashPropagation_UnknownResult]: "
                                                 "delegated_crash_propagation returned an unsupported "
                                                 "classification.  Expected Recovers<RecoveryProto>, "
                                                 "MustAbort, or IllFormed.");
    }
};

template <typename RecoveryProto>
struct propagation_assertion<Recovers<RecoveryProto>> {
    static consteval void check() noexcept {}
};

template <>
struct propagation_assertion<MustAbort> {
    template <typename Actual = MustAbort>
    static consteval void check() noexcept {
        static_assert(dependent_false_v<Actual>, "crucible::session::diagnostic "
                                                 "[DelegatedCrashPropagation_MissingRecovery]: "
                                                 "delegated_crash_propagation rejects: recipient is "
                                                 "unreliable but carrier K has no crash-recovery branch.");
    }
};

template <>
struct propagation_assertion<IllFormed> {
    template <typename Actual = IllFormed>
    static consteval void check() noexcept {
        static_assert(dependent_false_v<Actual>, "crucible::session::diagnostic "
                                                 "[DelegatedCrashPropagation_PrimaryTemplate]: "
                                                 "delegated_crash_propagation<T, R, K> primary template "
                                                 "fires -- specialize for your delegated type.");
    }
};

}  // namespace detail::delegate_crash

template <typename T, typename RecipientTag, typename CarrierK>
consteval void assert_delegated_crash_propagates() noexcept {
    using Result = delegated_crash_propagation_t<T, RecipientTag, CarrierK>;
    detail::delegate_crash::propagation_assertion<Result>::check();
}

}  // namespace crucible::safety::proto

// The two sides are checked differently.  A delegator has to clear both its
// own continuation and the obligation the handoff leaves behind.  A recipient
// only clears its continuation, because the endpoint it just received is
// checked later, when it is run as a protocol in its own right.

namespace crucible::safety::proto::detail::crash {

template <typename T, typename K, typename PeerTag>
struct all_offers_have_crash_branch<Delegate<T, K>, PeerTag>
    : std::bool_constant<
          all_offers_have_crash_branch<K, PeerTag>::value&& ::crucible::safety::proto::detail::delegate_crash::
              propagation_is_recoverable_v<delegated_crash_propagation_t<T, PeerTag, K>>> {};

template <typename T, typename K, typename PeerTag>
struct all_offers_have_crash_branch<Accept<T, K>, PeerTag> : all_offers_have_crash_branch<K, PeerTag> {};

template <typename T, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration, typename PeerTag>
struct all_offers_have_crash_branch<EpochedDelegate<T, K, MinEpoch, MinGeneration>, PeerTag>
    : std::bool_constant<
          all_offers_have_crash_branch<K, PeerTag>::value&& ::crucible::safety::proto::detail::delegate_crash::
              propagation_is_recoverable_v<delegated_crash_propagation_t<T, PeerTag, K>>> {};

template <typename T, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration, typename PeerTag>
struct all_offers_have_crash_branch<EpochedAccept<T, K, MinEpoch, MinGeneration>, PeerTag>
    : all_offers_have_crash_branch<K, PeerTag> {};

}  // namespace crucible::safety::proto::detail::crash

namespace crucible::safety::proto {

namespace detail {

template <typename... Ts>
struct delegate_seq_helper;

template <typename K>
struct delegate_seq_helper<K> {
    using type = K;
};

template <typename Head, typename... Rest>
struct delegate_seq_helper<Head, Rest...> {
    using type = Delegate<Head, typename delegate_seq_helper<Rest...>::type>;
};

template <typename... Ts>
struct accept_seq_helper;

template <typename K>
struct accept_seq_helper<K> {
    using type = K;
};

template <typename Head, typename... Rest>
struct accept_seq_helper<Head, Rest...> {
    using type = Accept<Head, typename accept_seq_helper<Rest...>::type>;
};

}  // namespace detail

// The last type argument is the continuation and every earlier one is a
// delegated protocol, in handoff order.  A single argument is therefore the
// continuation alone, and the empty form is ill-formed.
template <typename... Ts>
using Delegate_seq = typename detail::delegate_seq_helper<Ts...>::type;

template <typename... Ts>
using Accept_seq = typename detail::accept_seq_helper<Ts...>::type;

// A carrier that redelegates owns the inner session only in transit.  It never
// speaks that protocol, it only moves the endpoint along.
template <typename T, typename K>
using Redelegate = Accept<T, Delegate<T, K>>;

template <typename T, typename Ack, typename K>
using DelegateWithAck = Delegate<T, Recv<Ack, K>>;

template <typename T, typename Ack, typename K>
using AcceptWithAck = Accept<T, Send<Ack, K>>;

template <typename P>
struct is_delegate : std::false_type {};
template <typename T, typename K>
struct is_delegate<Delegate<T, K>> : std::true_type {};
template <typename T, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
struct is_delegate<EpochedDelegate<T, K, MinEpoch, MinGeneration>> : std::true_type {};

template <typename P>
struct is_accept : std::false_type {};
template <typename T, typename K>
struct is_accept<Accept<T, K>> : std::true_type {};
template <typename T, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
struct is_accept<EpochedAccept<T, K, MinEpoch, MinGeneration>> : std::true_type {};

template <typename P>
inline constexpr bool is_delegate_v = is_delegate<P>::value;
template <typename P>
inline constexpr bool is_accept_v = is_accept<P>::value;

template <typename P>
inline constexpr bool is_delegation_head_v = is_delegate_v<P> || is_accept_v<P>;

template <typename T, typename K>
struct dual_of<Delegate<T, K>> {
    using type = Accept<T, typename dual_of<K>::type>;
};

template <typename T, typename K>
struct dual_of<Accept<T, K>> {
    using type = Delegate<T, typename dual_of<K>::type>;
};

template <typename T, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
struct dual_of<EpochedDelegate<T, K, MinEpoch, MinGeneration>> {
    using type = EpochedAccept<T, typename dual_of<K>::type, MinEpoch, MinGeneration>;
};

template <typename T, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
struct dual_of<EpochedAccept<T, K, MinEpoch, MinGeneration>> {
    using type = EpochedDelegate<T, typename dual_of<K>::type, MinEpoch, MinGeneration>;
};

// Without these, the default answer would be true for every delegation shape,
// including one whose delegated protocol or continuation carries a
// Sender-annotated Offer.  That shape loses its annotation on the round trip
// and so is not involutive, and any rewrite gated on involution would then
// produce the wrong type.
//
// The round trip on the continuation alone would justify checking only K,
// since the delegated protocol is carried verbatim.  The conjunction over both
// is deliberate and stricter: unlike a payload, the delegated protocol is a
// session the recipient goes on to run, and a rewrite that dualizes past the
// handoff needs it involutive too.

template <typename T, typename K>
struct is_dual_involutive<Delegate<T, K>>
    : std::bool_constant<is_dual_involutive<T>::value && is_dual_involutive<K>::value> {};

template <typename T, typename K>
struct is_dual_involutive<Accept<T, K>>
    : std::bool_constant<is_dual_involutive<T>::value && is_dual_involutive<K>::value> {};

template <typename T, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
struct is_dual_involutive<EpochedDelegate<T, K, MinEpoch, MinGeneration>>
    : std::bool_constant<is_dual_involutive<T>::value && is_dual_involutive<K>::value> {};

template <typename T, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
struct is_dual_involutive<EpochedAccept<T, K, MinEpoch, MinGeneration>>
    : std::bool_constant<is_dual_involutive<T>::value && is_dual_involutive<K>::value> {};

// The terminal that composition replaces lives in the continuation, never in
// the delegated protocol, so composing leaves the delegated protocol alone.
//
// A handoff of an already-crashed endpoint is the exception: the channel is
// bottom, so neither side advances into its continuation and composition
// collapses to the crashed terminal on both the delegating and the accepting
// side.  Letting the accepting side compose normally would claim a recipient
// runs on past a crashed handoff, and would also break the identity that
// dualizing a composition equals composing the duals, since the delegating
// side already collapses.
//
// The right-hand position is handled at the point where composition
// substitutes for the terminal.  Structured protocols then reach it through
// the ordinary recursion, with no cross-shape specialization needed.

template <typename T, typename K, typename Q>
struct compose<Delegate<T, K>, Q> {
    using type = Delegate<T, typename compose<K, Q>::type>;
};

template <CrashClass C, typename K, typename Q>
struct compose<Delegate<Stop_g<C>, K>, Q> {
    using type = typename compose<Stop_g<C>, Q>::type;
};

template <CrashClass C, typename K>
struct compose<End, Delegate<Stop_g<C>, K>> {
    using type = Stop_g<C>;
};

template <typename T, typename K, typename Q>
struct compose<Accept<T, K>, Q> {
    using type = Accept<T, typename compose<K, Q>::type>;
};

template <CrashClass C, typename K, typename Q>
struct compose<Accept<Stop_g<C>, K>, Q> {
    using type = typename compose<Stop_g<C>, Q>::type;
};

// Without this, the terminal would resolve to the accept itself, which reads
// as "the protocol completes, and the crashed handoff is still ahead".
template <CrashClass C, typename K>
struct compose<End, Accept<Stop_g<C>, K>> {
    using type = Stop_g<C>;
};

template <typename T, typename K, typename Q, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
struct compose<EpochedDelegate<T, K, MinEpoch, MinGeneration>, Q> {
    using type = EpochedDelegate<T, typename compose<K, Q>::type, MinEpoch, MinGeneration>;
};

template <CrashClass C, typename K, typename Q, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
struct compose<EpochedDelegate<Stop_g<C>, K, MinEpoch, MinGeneration>, Q> {
    using type = typename compose<Stop_g<C>, Q>::type;
};

template <CrashClass C, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
struct compose<End, EpochedDelegate<Stop_g<C>, K, MinEpoch, MinGeneration>> {
    using type = Stop_g<C>;
};

template <typename T, typename K, typename Q, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
struct compose<EpochedAccept<T, K, MinEpoch, MinGeneration>, Q> {
    using type = EpochedAccept<T, typename compose<K, Q>::type, MinEpoch, MinGeneration>;
};

// The threshold is dropped along with the rest: a crashed channel has no
// freshness discipline left to enforce.
template <CrashClass C, typename K, typename Q, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
struct compose<EpochedAccept<Stop_g<C>, K, MinEpoch, MinGeneration>, Q> {
    using type = typename compose<Stop_g<C>, Q>::type;
};

template <CrashClass C, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
struct compose<End, EpochedAccept<Stop_g<C>, K, MinEpoch, MinGeneration>> {
    using type = Stop_g<C>;
};

// A handoff of a crashed endpoint refines its own continuation, because the
// stopped endpoint can produce no behaviour the continuation does not already
// admit.  This holds at the type level only.  The corresponding handle
// operation is deleted, so the relation cannot be used to obtain a live
// continuation from a crashed handoff.

template <CrashClass C, typename K>
struct is_subtype_sync_structural<Delegate<Stop_g<C>, K>, K> : std::true_type {};

template <CrashClass C, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
struct is_subtype_sync_structural<EpochedDelegate<Stop_g<C>, K, MinEpoch, MinGeneration>, K> : std::true_type {};

template <typename T1, typename K1, typename T2, typename K2, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
struct is_subtype_sync_structural<EpochedDelegate<T1, K1, MinEpoch, MinGeneration>,
                                  EpochedDelegate<T2, K2, MinEpoch, MinGeneration>>
    : std::bool_constant<is_subtype_sync_structural<T1, T2>::value && is_subtype_sync_structural<K1, K2>::value> {};

template <typename T1, typename K1, typename T2, typename K2, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
struct is_subtype_sync_structural<EpochedAccept<T1, K1, MinEpoch, MinGeneration>,
                                  EpochedAccept<T2, K2, MinEpoch, MinGeneration>>
    : std::bool_constant<is_subtype_sync_structural<T1, T2>::value && is_subtype_sync_structural<K1, K2>::value> {};

namespace detail::subtype {

template <CrashClass C, typename K>
struct protocol_grade_satisfies<Delegate<Stop_g<C>, K>, K> : std::true_type {};

template <CrashClass C, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
struct protocol_grade_satisfies<EpochedDelegate<Stop_g<C>, K, MinEpoch, MinGeneration>, K> : std::true_type {};

template <typename T1, typename K1, typename T2, typename K2, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
struct protocol_grade_satisfies<EpochedDelegate<T1, K1, MinEpoch, MinGeneration>,
                                EpochedDelegate<T2, K2, MinEpoch, MinGeneration>>
    : std::bool_constant<protocol_grade_satisfies<T1, T2>::value && protocol_grade_satisfies<K1, K2>::value> {};

template <typename T1, typename K1, typename T2, typename K2, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
struct protocol_grade_satisfies<EpochedAccept<T1, K1, MinEpoch, MinGeneration>,
                                EpochedAccept<T2, K2, MinEpoch, MinGeneration>>
    : std::bool_constant<protocol_grade_satisfies<T1, T2>::value && protocol_grade_satisfies<K1, K2>::value> {};

}  // namespace detail::subtype

// The delegated protocol is checked in an empty loop context, not the
// delegator's.  It travels to another participant, so a loop edge inside it
// cannot refer to a loop the delegator happens to be in.  It may of course
// carry loops of its own.  This is stricter than a plain send, which checks
// only its continuation.

template <typename T, typename K, typename LoopCtx>
struct is_well_formed<Delegate<T, K>, LoopCtx>
    : std::bool_constant<is_well_formed<T, void>::value && is_well_formed<K, LoopCtx>::value> {};

template <typename T, typename K, typename LoopCtx>
struct is_well_formed<Accept<T, K>, LoopCtx>
    : std::bool_constant<is_well_formed<T, void>::value && is_well_formed<K, LoopCtx>::value> {};

template <typename T, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration, typename LoopCtx>
struct is_well_formed<EpochedDelegate<T, K, MinEpoch, MinGeneration>, LoopCtx>
    : std::bool_constant<session_epoch_threshold_valid_v<LoopCtx, MinEpoch, MinGeneration>
                         && is_well_formed<T, void>::value && is_well_formed<K, LoopCtx>::value
                         && (!session_loop_ctx_has_explicit_epoch_v<LoopCtx>
                             || session_loop_ctx_epoch_matches_v<LoopCtx, MinEpoch, MinGeneration>)> {};

template <typename T, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration, typename LoopCtx>
struct is_well_formed<EpochedAccept<T, K, MinEpoch, MinGeneration>, LoopCtx>
    : std::bool_constant<session_epoch_threshold_valid_v<LoopCtx, MinEpoch, MinGeneration>
                         && is_well_formed<T, void>::value && is_well_formed<K, LoopCtx>::value
                         && session_loop_ctx_epoch_satisfies_v<LoopCtx, MinEpoch, MinGeneration>> {};

// An empty choice in either arm is reachable once the handoff fires.  The
// delegated protocol becomes a session of its own, where an empty choice
// leaves its holder stuck, and the continuation runs on the outer handle.

template <typename T, typename K>
struct is_empty_choice<Delegate<T, K>> : std::bool_constant<is_empty_choice<T>::value || is_empty_choice<K>::value> {};

template <typename T, typename K>
struct is_empty_choice<Accept<T, K>> : std::bool_constant<is_empty_choice<T>::value || is_empty_choice<K>::value> {};

template <typename T, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
struct is_empty_choice<EpochedDelegate<T, K, MinEpoch, MinGeneration>>
    : std::bool_constant<is_empty_choice<T>::value || is_empty_choice<K>::value> {};

template <typename T, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
struct is_empty_choice<EpochedAccept<T, K, MinEpoch, MinGeneration>>
    : std::bool_constant<is_empty_choice<T>::value || is_empty_choice<K>::value> {};

template <typename T, typename K, typename Resource, typename LoopCtx>
class [[nodiscard]] SessionHandle<Delegate<T, K>, Resource, LoopCtx>
    : public SessionHandleBase<Delegate<T, K>, SessionHandle<Delegate<T, K>, Resource, LoopCtx>> {
    Resource resource_;

    template <typename P, typename Res, typename L>
    friend class SessionHandle;
    // The private constructor and this single friend leave one authorized way
    // to build a handle.  Direct construction is rejected, so the minting and
    // step gates cannot be bypassed.
    template <typename FProto, typename FRes, typename FLoop>
    friend constexpr auto
        detail::make_session_handle(FRes, std::source_location) noexcept(std::is_nothrow_move_constructible_v<FRes>)
            -> SessionHandle<FProto, FRes, FLoop>;

    constexpr explicit SessionHandle(Resource r, std::source_location loc = std::source_location::current()) noexcept(
        std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Delegate<T, K>, SessionHandle<Delegate<T, K>, Resource, LoopCtx>>{loc},
          resource_{std::move(r)} {}

public:
    using protocol = Delegate<T, K>;
    using delegated_proto = T;
    using continuation = K;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;

    constexpr SessionHandle(SessionHandle&&) noexcept = default;
    constexpr SessionHandle& operator=(SessionHandle&&) noexcept = default;
    ~SessionHandle() = default;

    // Both handles are consumed: this one and the one being handed off.  The
    // transport is what actually moves the endpoint to the peer, in whatever
    // form the medium requires.  The type system tracks the ownership change
    // and nothing else.
    template <typename DelegatedResource, typename DelegatedLoopCtx, typename Transport>
        requires(!is_stop_v<T> && std::is_invocable_v<Transport, Resource&, DelegatedResource &&>)
    [[nodiscard]] constexpr auto
    delegate(SessionHandle<T, DelegatedResource, DelegatedLoopCtx>&& delegated,
             Transport transport) && noexcept(std::is_nothrow_invocable_v<Transport, Resource&, DelegatedResource&&>
                                              && std::is_nothrow_move_constructible_v<Resource>) {
        std::invoke(transport, resource_, std::move(delegated.resource_));
        delegated.mark_consumed_();
        this->mark_consumed_();
        return detail::step_to_next<K, Resource, LoopCtx>(std::move(resource_));
    }

    template <typename DelegatedResource, typename DelegatedLoopCtx, typename Transport>
        requires is_stop_v<T>
    void delegate(SessionHandle<T, DelegatedResource, DelegatedLoopCtx>&&,
                  Transport) && = delete("[DelegateStop_NoContinuation] SessionHandle<Delegate<Stop, K>> "
                                         "cannot delegate an already-crashed endpoint and continue as K.  "
                                         "The type-level compose rule collapses Delegate<Stop, K> to Stop; "
                                         "recover by handling Stop/crash before this handoff point instead "
                                         "of expecting K's continuation-side authority.");

    // Nothing is shipped here.  A peer waiting on the matching accept never
    // receives an endpoint, so this is only correct where the peer obtains it
    // some other way, or does not need it at all.  The name carries that
    // absence to every call site.
    //
    // The delegated handle's resource is destroyed on scope exit.  If it owns
    // a live wire, that wire closes.  Use the transport-taking form to keep it
    // open.
    template <typename DelegatedResource, typename DelegatedLoopCtx>
        requires(!is_stop_v<T>)
    [[nodiscard]] constexpr auto
    delegate_local(SessionHandle<T, DelegatedResource, DelegatedLoopCtx>&& delegated) && noexcept(
        std::is_nothrow_move_constructible_v<Resource> && std::is_nothrow_destructible_v<DelegatedResource>) {
        delegated.mark_consumed_();
        (void)std::move(delegated);
        this->mark_consumed_();
        return detail::step_to_next<K, Resource, LoopCtx>(std::move(resource_));
    }

    template <typename DelegatedResource, typename DelegatedLoopCtx>
        requires is_stop_v<T>
    void delegate_local(SessionHandle<T, DelegatedResource, DelegatedLoopCtx>&&) && =
        delete("[DelegateStop_NoContinuation] SessionHandle<Delegate<Stop, K>> "
               "cannot locally delegate an already-crashed endpoint and continue "
               "as K.  Delegate<Stop, K> collapses to Stop; handle recovery "
               "before this state.");

    template <typename DelegatedResource, typename DelegatedLoopCtx>
    void delegate(SessionHandle<T, DelegatedResource, DelegatedLoopCtx>&&) && =
        delete("[Wire_Variant_Required] SessionHandle<Delegate<T, K>>::"
               "delegate(handle) without a transport is not allowed.  The "
               "Delegate combinator's semantic is \"ship the "
               "delegated endpoint to the peer\"; omitting the transport "
               "means nothing is shipped and the peer's corresponding Accept "
               "call hangs forever.  Choose one: "
               "(a) `.delegate(handle, transport)` — the transport callable "
               "physically ships the endpoint (RDMA put, fd passing, bytes "
               "over the wire, etc.) and the peer's Accept receives it, OR "
               "(b) `.delegate_local(handle)` — explicitly no wire transfer, "
               "for in-memory channels or test stubs where the peer obtains "
               "the endpoint via a separate path or doesn't need it at all.  "
               "The framework refuses to guess which you meant.");

    [[nodiscard]] constexpr Resource& resource() & noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return resource_; }
};

// The received endpoint is wrapped in a handle with an empty loop context,
// because the delegated protocol stands on its own and does not sit inside any
// loop this participant is running.

template <typename T, typename K, typename Resource, typename LoopCtx>
class [[nodiscard]] SessionHandle<Accept<T, K>, Resource, LoopCtx>
    : public SessionHandleBase<Accept<T, K>, SessionHandle<Accept<T, K>, Resource, LoopCtx>> {
    Resource resource_;

    template <typename P, typename Res, typename L>
    friend class SessionHandle;
    template <typename FProto, typename FRes, typename FLoop>
    friend constexpr auto
        detail::make_session_handle(FRes, std::source_location) noexcept(std::is_nothrow_move_constructible_v<FRes>)
            -> SessionHandle<FProto, FRes, FLoop>;

    constexpr explicit SessionHandle(Resource r, std::source_location loc = std::source_location::current()) noexcept(
        std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Accept<T, K>, SessionHandle<Accept<T, K>, Resource, LoopCtx>>{loc},
          resource_{std::move(r)} {}

public:
    using protocol = Accept<T, K>;
    using delegated_proto = T;
    using continuation = K;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;

    constexpr SessionHandle(SessionHandle&&) noexcept = default;
    constexpr SessionHandle& operator=(SessionHandle&&) noexcept = default;
    ~SessionHandle() = default;

    template <typename Transport, typename DelegatedResource = std::invoke_result_t<Transport, Resource&>>
        requires std::is_invocable_v<Transport, Resource&>
    [[nodiscard]] constexpr auto
    accept(Transport transport) && noexcept(std::is_nothrow_invocable_v<Transport, Resource&>
                                            && std::is_nothrow_move_constructible_v<Resource>
                                            && std::is_nothrow_move_constructible_v<DelegatedResource>) {
        DelegatedResource delegated_res = std::invoke(transport, resource_);
        this->mark_consumed_();
        auto delegated_handle = mint_session_handle<T>(std::move(delegated_res));
        auto continuation_handle = detail::step_to_next<K, Resource, LoopCtx>(std::move(resource_));
        return std::pair{std::move(delegated_handle), std::move(continuation_handle)};
    }

    // The caller supplies the endpoint instead of a transport, so no receive
    // happens here and the call is only a type-state advance.
    template <typename DelegatedResource>
    [[nodiscard]] constexpr auto accept_with(DelegatedResource delegated_res) && noexcept(
        std::is_nothrow_move_constructible_v<Resource> && std::is_nothrow_move_constructible_v<DelegatedResource>) {
        this->mark_consumed_();
        auto delegated_handle = mint_session_handle<T>(std::move(delegated_res));
        auto continuation_handle = detail::step_to_next<K, Resource, LoopCtx>(std::move(resource_));
        return std::pair{std::move(delegated_handle), std::move(continuation_handle)};
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return resource_; }
};

// Nothing on the sending side checks the threshold.  This handle only carries
// it, so that duality, logs and diagnostics keep the freshness requirement
// visible in the type until the recipient enforces it.

template <typename T, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration, typename Resource,
          typename LoopCtx>
class [[nodiscard]] SessionHandle<EpochedDelegate<T, K, MinEpoch, MinGeneration>, Resource, LoopCtx>
    : public SessionHandleBase<EpochedDelegate<T, K, MinEpoch, MinGeneration>,
                               SessionHandle<EpochedDelegate<T, K, MinEpoch, MinGeneration>, Resource, LoopCtx>> {
    using Protocol = EpochedDelegate<T, K, MinEpoch, MinGeneration>;

    Resource resource_;

    template <typename P, typename Res, typename L>
    friend class SessionHandle;
    template <typename FProto, typename FRes, typename FLoop>
    friend constexpr auto
        detail::make_session_handle(FRes, std::source_location) noexcept(std::is_nothrow_move_constructible_v<FRes>)
            -> SessionHandle<FProto, FRes, FLoop>;

    constexpr explicit SessionHandle(Resource r, std::source_location loc = std::source_location::current()) noexcept(
        std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Protocol, SessionHandle<Protocol, Resource, LoopCtx>>{loc}, resource_{std::move(r)} {}

public:
    using protocol = Protocol;
    using delegated_proto = T;
    using continuation = K;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    static constexpr std::uint64_t min_epoch = MinEpoch;
    static constexpr std::uint64_t min_generation = MinGeneration;

    constexpr SessionHandle(SessionHandle&&) noexcept = default;
    constexpr SessionHandle& operator=(SessionHandle&&) noexcept = default;
    ~SessionHandle() = default;

    template <typename DelegatedResource, typename DelegatedLoopCtx, typename Transport>
        requires(!is_stop_v<T> && std::is_invocable_v<Transport, Resource&, DelegatedResource &&>)
    [[nodiscard]] constexpr auto
    delegate(SessionHandle<T, DelegatedResource, DelegatedLoopCtx>&& delegated,
             Transport transport) && noexcept(std::is_nothrow_invocable_v<Transport, Resource&, DelegatedResource&&>
                                              && std::is_nothrow_move_constructible_v<Resource>) {
        std::invoke(transport, resource_, std::move(delegated.resource_));
        delegated.mark_consumed_();
        this->mark_consumed_();
        return detail::step_to_next<K, Resource, LoopCtx>(std::move(resource_));
    }

    template <typename DelegatedResource, typename DelegatedLoopCtx, typename Transport>
        requires is_stop_v<T>
    void delegate(SessionHandle<T, DelegatedResource, DelegatedLoopCtx>&&,
                  Transport) && = delete("[DelegateStop_NoContinuation] SessionHandle<EpochedDelegate<"
                                         "Stop, K, MinEpoch, MinGeneration>> cannot delegate an already-"
                                         "crashed endpoint and continue as K.  Handle Stop/crash before "
                                         "this epoch-versioned handoff point.");

    template <typename DelegatedResource, typename DelegatedLoopCtx>
        requires(!is_stop_v<T>)
    [[nodiscard]] constexpr auto
    delegate_local(SessionHandle<T, DelegatedResource, DelegatedLoopCtx>&& delegated) && noexcept(
        std::is_nothrow_move_constructible_v<Resource> && std::is_nothrow_destructible_v<DelegatedResource>) {
        delegated.mark_consumed_();
        (void)std::move(delegated);
        this->mark_consumed_();
        return detail::step_to_next<K, Resource, LoopCtx>(std::move(resource_));
    }

    template <typename DelegatedResource, typename DelegatedLoopCtx>
        requires is_stop_v<T>
    void delegate_local(SessionHandle<T, DelegatedResource, DelegatedLoopCtx>&&) && =
        delete("[DelegateStop_NoContinuation] SessionHandle<EpochedDelegate<"
               "Stop, K, MinEpoch, MinGeneration>> cannot locally delegate an "
               "already-crashed endpoint and continue as K.");

    template <typename DelegatedResource, typename DelegatedLoopCtx>
    void delegate(SessionHandle<T, DelegatedResource, DelegatedLoopCtx>&&) && =
        delete("[Wire_Variant_Required] SessionHandle<EpochedDelegate<T, K, "
               "MinEpoch, MinGeneration>>::delegate(handle) without a transport "
               "is not allowed.  Choose delegate(handle, transport) for wire "
               "handoff or delegate_local(handle) for explicit in-memory tests.");

    [[nodiscard]] constexpr Resource& resource() & noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return resource_; }
};

template <typename T, typename K, std::uint64_t MinEpoch, std::uint64_t MinGeneration, typename Resource,
          typename LoopCtx>
class [[nodiscard]] SessionHandle<EpochedAccept<T, K, MinEpoch, MinGeneration>, Resource, LoopCtx>
    : public SessionHandleBase<EpochedAccept<T, K, MinEpoch, MinGeneration>,
                               SessionHandle<EpochedAccept<T, K, MinEpoch, MinGeneration>, Resource, LoopCtx>> {
    using Protocol = EpochedAccept<T, K, MinEpoch, MinGeneration>;

    static_assert(session_loop_ctx_epoch_satisfies_v<LoopCtx, MinEpoch, MinGeneration>,
                  "crucible::session::diagnostic [EpochCtx_StaleRecipient]: "
                  "SessionHandle<EpochedAccept<T, K, MinEpoch, MinGeneration>> "
                  "requires LoopCtx = EpochCtx<CurrentEpoch, CurrentGeneration, ...> "
                  "with CurrentEpoch >= MinEpoch and CurrentGeneration >= "
                  "MinGeneration.  A stale or unannotated recipient cannot accept "
                  "this delegated endpoint.");

    Resource resource_;

    template <typename P, typename Res, typename L>
    friend class SessionHandle;
    // This is the one specialization the ordinary mint cannot produce: it
    // always builds an empty loop context, and this handle demands one
    // carrying an epoch.  The recipient path therefore goes through the
    // factory directly, which stays the only authorized construction site.
    template <typename FProto, typename FRes, typename FLoop>
    friend constexpr auto
        detail::make_session_handle(FRes, std::source_location) noexcept(std::is_nothrow_move_constructible_v<FRes>)
            -> SessionHandle<FProto, FRes, FLoop>;

    constexpr explicit SessionHandle(Resource r, std::source_location loc = std::source_location::current()) noexcept(
        std::is_nothrow_move_constructible_v<Resource>)
        : SessionHandleBase<Protocol, SessionHandle<Protocol, Resource, LoopCtx>>{loc}, resource_{std::move(r)} {}

public:
    using protocol = Protocol;
    using delegated_proto = T;
    using continuation = K;
    using resource_type = Resource;
    using loop_ctx = LoopCtx;
    static constexpr std::uint64_t min_epoch = MinEpoch;
    static constexpr std::uint64_t min_generation = MinGeneration;

    constexpr SessionHandle(SessionHandle&&) noexcept = default;
    constexpr SessionHandle& operator=(SessionHandle&&) noexcept = default;
    ~SessionHandle() = default;

    template <typename Transport, typename DelegatedResource = std::invoke_result_t<Transport, Resource&>>
        requires std::is_invocable_v<Transport, Resource&>
    [[nodiscard]] constexpr auto
    accept(Transport transport) && noexcept(std::is_nothrow_invocable_v<Transport, Resource&>
                                            && std::is_nothrow_move_constructible_v<Resource>
                                            && std::is_nothrow_move_constructible_v<DelegatedResource>) {
        DelegatedResource delegated_res = std::invoke(transport, resource_);
        this->mark_consumed_();
        auto delegated_handle = mint_session_handle<T>(std::move(delegated_res));
        auto continuation_handle = detail::step_to_next<K, Resource, LoopCtx>(std::move(resource_));
        return std::pair{std::move(delegated_handle), std::move(continuation_handle)};
    }

    template <typename DelegatedResource>
    [[nodiscard]] constexpr auto accept_with(DelegatedResource delegated_res) && noexcept(
        std::is_nothrow_move_constructible_v<Resource> && std::is_nothrow_move_constructible_v<DelegatedResource>) {
        this->mark_consumed_();
        auto delegated_handle = mint_session_handle<T>(std::move(delegated_res));
        auto continuation_handle = detail::step_to_next<K, Resource, LoopCtx>(std::move(resource_));
        return std::pair{std::move(delegated_handle), std::move(continuation_handle)};
    }

    [[nodiscard]] constexpr Resource& resource() & noexcept { return resource_; }
    [[nodiscard]] constexpr const Resource& resource() const& noexcept { return resource_; }
};

// A closed protocol is delegate-compatible by default, because the handoff
// moves a handle and nothing else.  A protocol family that carries authority
// beyond its handle specializes this to false until its handoff semantics are
// spelled out.
template <typename Proto>
struct is_delegate_compatible : std::bool_constant<is_well_formed_v<Proto>> {};

template <typename Proto>
inline constexpr bool is_delegate_compatible_v = is_delegate_compatible<Proto>::value;

template <typename Proto, typename RecipientTag>
inline constexpr bool can_delegate_v = is_delegate_compatible_v<Proto> && is_well_formed_v<Proto>;

template <typename Proto, typename RecipientTag>
concept CanDelegate = can_delegate_v<Proto, RecipientTag>;

template <typename CarrierProto, typename DelegatedProto>
concept DelegatesTo =
    is_delegate_v<CarrierProto> && std::is_same_v<typename CarrierProto::delegated_proto, DelegatedProto>;

template <typename CarrierProto, typename DelegatedProto>
concept AcceptsFrom =
    is_accept_v<CarrierProto> && std::is_same_v<typename CarrierProto::delegated_proto, DelegatedProto>;

// Constraining a user-supplied transport at a boundary puts the diagnostic at
// the call site rather than inside the handoff's instantiation.
template <typename Transport, typename CarrierRes, typename DelegatedRes>
concept TransportForDelegate = std::is_invocable_v<Transport, CarrierRes&, DelegatedRes&&>;

// The return type is checked as well as invocability.  A transport returning
// the wrong type would otherwise be accepted, silently redefining the deduced
// endpoint type through the defaulted template parameter on the accept call.
template <typename Transport, typename CarrierRes, typename DelegatedRes>
concept TransportForAccept = std::is_invocable_v<Transport, CarrierRes&>
                          && std::is_same_v<std::invoke_result_t<Transport, CarrierRes&>, DelegatedRes>;

// These place the diagnostic at the assertion site instead of deep inside a
// template instantiation.
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
    static_assert(AcceptsFrom<CarrierProto, DelegatedProto>, "crucible::session::diagnostic [ProtocolViolation_State]: "
                                                             "assert_accepts_from: CarrierProto must be "
                                                             "Accept<DelegatedProto, K> for some K.");
}

#ifdef CRUCIBLE_SESSION_SELF_TESTS
namespace detail::delegate_self_test {

struct Req {};
struct Ack {};
using DelegatedProto = Send<Req, Recv<Ack, End>>;

static_assert(std::is_same_v<dual_of_t<Delegate<DelegatedProto, End>>, Accept<DelegatedProto, End>>);

static_assert(std::is_same_v<dual_of_t<Accept<DelegatedProto, End>>, Delegate<DelegatedProto, End>>);

static_assert(std::is_same_v<dual_of_t<dual_of_t<Delegate<DelegatedProto, Send<int, End>>>>,
                             Delegate<DelegatedProto, Send<int, End>>>);

// The delegated protocol here is asymmetric: its own dual is Recv<Req, End>.
// The assertion below shows it crossing the dual unchanged.
using DelegatedAsymmetric = Send<Req, End>;
static_assert(std::is_same_v<dual_of_t<Delegate<DelegatedAsymmetric, End>>, Accept<DelegatedAsymmetric, End>>);

static_assert(
    std::is_same_v<dual_of_t<Delegate<DelegatedProto, Send<int, End>>>, Accept<DelegatedProto, Recv<int, End>>>);

using EpochedDelegator = EpochedDelegate<DelegatedProto, Recv<Ack, End>, 5, 3>;
using EpochedAcceptor = EpochedAccept<DelegatedProto, Send<Ack, End>, 5, 3>;

static_assert(std::is_same_v<dual_of_t<EpochedDelegator>, EpochedAcceptor>);
static_assert(std::is_same_v<dual_of_t<EpochedAcceptor>, EpochedDelegator>);
static_assert(std::is_same_v<dual_of_t<dual_of_t<EpochedDelegator>>, EpochedDelegator>);
static_assert(EpochedDelegator::min_epoch == 5);
static_assert(EpochedDelegator::min_generation == 3);

static_assert(
    std::is_same_v<compose_t<Delegate<DelegatedProto, End>, Send<int, End>>, Delegate<DelegatedProto, Send<int, End>>>);

static_assert(std::is_same_v<compose_t<Delegate<DelegatedProto, Send<int, End>>, Recv<bool, End>>,
                             Delegate<DelegatedProto, Send<int, Recv<bool, End>>>>);

static_assert(std::is_same_v<compose_t<Accept<DelegatedAsymmetric, End>, End>, Accept<DelegatedAsymmetric, End>>);

static_assert(std::is_same_v<compose_t<EpochedDelegator, Send<int, End>>,
                             EpochedDelegate<DelegatedProto, Recv<Ack, Send<int, End>>, 5, 3>>);

static_assert(std::is_same_v<compose_t<Delegate<Stop, Send<int, End>>, Recv<Ack, End>>, Stop>);
static_assert(std::is_same_v<compose_t<Delegate<Stop_g<CrashClass::Throw>, Send<int, End>>, Recv<Ack, End>>,
                             Stop_g<CrashClass::Throw>>);

using ComposeThenDelegateStop = compose_t<Send<int, End>, Delegate<Stop, Recv<Ack, End>>>;
static_assert(std::is_same_v<ComposeThenDelegateStop, Send<int, Stop>>);
static_assert(is_well_formed_v<ComposeThenDelegateStop>);

using ComposeThenDelegateStopG = compose_t<Send<int, End>, Delegate<Stop_g<CrashClass::ErrorReturn>, Recv<Ack, End>>>;
static_assert(std::is_same_v<ComposeThenDelegateStopG, Send<int, Stop_g<CrashClass::ErrorReturn>>>);
static_assert(is_well_formed_v<ComposeThenDelegateStopG>);

static_assert(std::is_same_v<compose_t<Stop, Delegate<Stop, Recv<Ack, End>>>, Stop>);
static_assert(std::is_same_v<compose_t<Delegate<Stop, Send<int, End>>, Stop>, Stop>);

static_assert(std::is_same_v<compose_t<Accept<Stop, Send<int, End>>, Recv<Ack, End>>, Stop>);

static_assert(std::is_same_v<compose_t<Accept<Stop_g<CrashClass::Abort>, Send<int, End>>, Recv<Ack, End>>,
                             Stop_g<CrashClass::Abort>>);
static_assert(std::is_same_v<compose_t<Accept<Stop_g<CrashClass::Throw>, Send<int, End>>, Recv<Ack, End>>,
                             Stop_g<CrashClass::Throw>>);
static_assert(std::is_same_v<compose_t<Accept<Stop_g<CrashClass::ErrorReturn>, Send<int, End>>, Recv<Ack, End>>,
                             Stop_g<CrashClass::ErrorReturn>>);
static_assert(std::is_same_v<compose_t<Accept<Stop_g<CrashClass::NoThrow>, Send<int, End>>, Recv<Ack, End>>,
                             Stop_g<CrashClass::NoThrow>>);

using ComposeThenAcceptStop = compose_t<Send<int, End>, Accept<Stop, Recv<Ack, End>>>;
static_assert(std::is_same_v<ComposeThenAcceptStop, Send<int, Stop>>);
static_assert(is_well_formed_v<ComposeThenAcceptStop>);

using ComposeThenAcceptStopG = compose_t<Send<int, End>, Accept<Stop_g<CrashClass::NoThrow>, Recv<Ack, End>>>;
static_assert(std::is_same_v<ComposeThenAcceptStopG, Send<int, Stop_g<CrashClass::NoThrow>>>);
static_assert(is_well_formed_v<ComposeThenAcceptStopG>);

static_assert(std::is_same_v<compose_t<Stop, Accept<Stop, Recv<Ack, End>>>, Stop>);
static_assert(std::is_same_v<compose_t<Accept<Stop, Send<int, End>>, Stop>, Stop>);

static_assert(std::is_same_v<compose_t<EpochedAccept<Stop_g<CrashClass::Throw>, End, 5, 3>, Recv<Ack, End>>,
                             Stop_g<CrashClass::Throw>>);
static_assert(std::is_same_v<compose_t<EpochedAccept<Stop_g<CrashClass::Abort>, Send<int, End>, 7, 2>, Recv<Ack, End>>,
                             Stop_g<CrashClass::Abort>>);
static_assert(std::is_same_v<compose_t<Send<int, End>, EpochedAccept<Stop_g<CrashClass::ErrorReturn>, End, 5, 3>>,
                             Send<int, Stop_g<CrashClass::ErrorReturn>>>);

// Duality and composition commute across a crashed handoff only because both
// sides collapse to the same self-dual terminal.
static_assert(
    std::is_same_v<dual_of_t<compose_t<Delegate<Stop_g<CrashClass::Throw>, End>, Send<int, End>>>,
                   compose_t<dual_of_t<Delegate<Stop_g<CrashClass::Throw>, End>>, dual_of_t<Send<int, End>>>>);

static_assert(is_subtype_sync_v<Delegate<Stop, Send<int, End>>, Send<int, End>>);

static_assert(is_dual_involutive_v<Delegate<Send<int, End>, End>>);
static_assert(is_dual_involutive_v<Accept<Send<int, End>, End>>);
static_assert(is_dual_involutive_v<EpochedDelegate<Send<int, End>, End, 5, 3>>);
static_assert(is_dual_involutive_v<EpochedAccept<Send<int, End>, End, 5, 3>>);

namespace fixy_a2_003_sender_offer_inner_T {
struct RoleA {};
using NonInvolutiveT = Offer<Sender<RoleA>, Recv<int, End>>;
static_assert(!is_dual_involutive_v<NonInvolutiveT>);
static_assert(!is_dual_involutive_v<Delegate<NonInvolutiveT, End>>);
static_assert(!is_dual_involutive_v<Accept<NonInvolutiveT, End>>);
static_assert(!is_dual_involutive_v<EpochedDelegate<NonInvolutiveT, End, 5, 3>>);
static_assert(!is_dual_involutive_v<EpochedAccept<NonInvolutiveT, End, 5, 3>>);
}  // namespace fixy_a2_003_sender_offer_inner_T

namespace fixy_a2_003_sender_offer_inner_K {
struct RoleA {};
using NonInvolutiveK = Offer<Sender<RoleA>, Recv<int, End>>;
static_assert(!is_dual_involutive_v<Delegate<Send<int, End>, NonInvolutiveK>>);
static_assert(!is_dual_involutive_v<Accept<Send<int, End>, NonInvolutiveK>>);
}  // namespace fixy_a2_003_sender_offer_inner_K

static_assert(is_well_formed_v<Delegate<DelegatedProto, End>>);
static_assert(is_well_formed_v<Accept<DelegatedProto, End>>);
static_assert(is_well_formed_v<EpochedDelegate<DelegatedProto, End, 5, 3>>);
static_assert(is_well_formed<EpochedDelegate<DelegatedProto, End, 5, 3>, EpochCtx<5, 3>>::value);
static_assert(!is_well_formed<EpochedDelegate<DelegatedProto, End, 5, 3>, EpochCtx<5, 2>>::value);
static_assert(is_well_formed<EpochedAccept<DelegatedProto, End, 5, 3>, EpochCtx<5, 3>>::value);
static_assert(is_well_formed<EpochedAccept<DelegatedProto, End, 5, 3>, EpochCtx<6, 3>>::value);
static_assert(!is_well_formed<EpochedAccept<DelegatedProto, End, 5, 3>, EpochCtx<4, 3>>::value);
static_assert(!is_well_formed_v<EpochedAccept<DelegatedProto, End, 5, 3>>);

static_assert(is_well_formed_v<Delegate<Delegate<Send<Req, End>, End>, End>>);

static_assert(!is_well_formed_v<Delegate<DelegatedProto, Continue>>);

static_assert(!is_well_formed_v<Delegate<Continue, End>>);

static_assert(is_well_formed_v<Loop<Delegate<DelegatedProto, Continue>>>);

static_assert(is_delegate_v<Delegate<DelegatedProto, End>>);
static_assert(!is_delegate_v<Accept<DelegatedProto, End>>);
static_assert(!is_delegate_v<Send<int, End>>);

static_assert(is_accept_v<Accept<DelegatedProto, End>>);
static_assert(!is_accept_v<Delegate<DelegatedProto, End>>);

static_assert(is_delegation_head_v<Delegate<DelegatedProto, End>>);
static_assert(is_delegation_head_v<Accept<DelegatedProto, End>>);
static_assert(is_delegation_head_v<EpochedDelegator>);
static_assert(is_delegation_head_v<EpochedAcceptor>);
static_assert(!is_delegation_head_v<Send<int, End>>);

namespace cntp_cross_layer_example {
struct SwimProbe {};
struct SwimAck {};
using SwimProto = Loop<Send<SwimProbe, Recv<SwimAck, Continue>>>;

struct RaftAppend {};
struct RaftAck {};
using RaftProto = Loop<Recv<RaftAppend, Send<RaftAck, Continue>>>;

struct CollectiveChunk {};
struct CollectiveAck {};
using CollectiveProto = Loop<Send<CollectiveChunk, Recv<CollectiveAck, Continue>>>;
using NvCollectiveProto = VendorPinned<VendorBackend::NV, CollectiveProto>;
using PortableCollectiveProto = VendorPinned<VendorBackend::Portable, CollectiveProto>;

// A transport protocol whose whole job is to hand out endpoints for the
// protocols layered on top of it.  Those protocols stay opaque to it.
using CntpLayer1 =
    VendorPinned<VendorBackend::Portable, Loop<Select<Delegate<SwimProto, Continue>, Delegate<RaftProto, Continue>,
                                                      Delegate<NvCollectiveProto, Continue>, End>>>;

static_assert(is_well_formed_v<CntpLayer1>);
static_assert(is_well_formed_v<PortableCollectiveProto>);

using CntpLayer1Peer = dual_of_t<CntpLayer1>;
static_assert(
    std::is_same_v<CntpLayer1Peer, VendorPinned<VendorBackend::Portable,
                                                Loop<Offer<Accept<SwimProto, Continue>, Accept<RaftProto, Continue>,
                                                           Accept<NvCollectiveProto, Continue>, End>>>>);
static_assert(is_well_formed_v<CntpLayer1Peer>);

static_assert(std::is_same_v<dual_of_t<CntpLayer1Peer>, CntpLayer1>);
}  // namespace cntp_cross_layer_example

using LoopedDelegator = Loop<Delegate<DelegatedProto, Continue>>;
using LoopedAcceptor = Loop<Accept<DelegatedProto, Continue>>;

static_assert(std::is_same_v<dual_of_t<LoopedDelegator>, LoopedAcceptor>);
static_assert(std::is_same_v<dual_of_t<LoopedAcceptor>, LoopedDelegator>);
static_assert(std::is_same_v<dual_of_t<dual_of_t<LoopedDelegator>>, LoopedDelegator>);
static_assert(is_well_formed_v<LoopedDelegator>);
static_assert(is_well_formed_v<LoopedAcceptor>);

static_assert(DelegatesTo<Delegate<DelegatedProto, End>, DelegatedProto>);
static_assert(!DelegatesTo<Delegate<DelegatedProto, End>, Send<int, End>>);
static_assert(!DelegatesTo<Accept<DelegatedProto, End>, DelegatedProto>);
static_assert(AcceptsFrom<Accept<DelegatedProto, End>, DelegatedProto>);
static_assert(DelegatesTo<EpochedDelegator, DelegatedProto>);
static_assert(AcceptsFrom<EpochedAcceptor, DelegatedProto>);

consteval bool check_assert_delegates() {
    assert_delegates_to<Delegate<DelegatedProto, End>, DelegatedProto>();
    assert_accepts_from<Accept<DelegatedProto, End>, DelegatedProto>();
    return true;
}
static_assert(check_assert_delegates());

struct DelegatedRecipient {};
using CarrierCrashRecovery = Offer<Recv<Ack, End>, Recv<Crash<DelegatedRecipient>, End>>;

static_assert(std::is_same_v<delegated_crash_propagation_t<Send<Req, End>, DelegatedRecipient, CarrierCrashRecovery>,
                             Recovers<End>>);

consteval bool check_assert_delegated_crash_propagates() {
    assert_delegated_crash_propagates<Send<Req, End>, DelegatedRecipient, CarrierCrashRecovery>();
    return true;
}
static_assert(check_assert_delegated_crash_propagates());

static_assert(std::is_same_v<Delegate_seq<End>, End>);
static_assert(std::is_same_v<Accept_seq<End>, End>);

static_assert(std::is_same_v<Delegate_seq<DelegatedProto, End>, Delegate<DelegatedProto, End>>);
static_assert(std::is_same_v<Accept_seq<DelegatedProto, End>, Accept<DelegatedProto, End>>);

static_assert(std::is_same_v<Delegate_seq<Send<Req, End>, Recv<Ack, End>, End>,
                             Delegate<Send<Req, End>, Delegate<Recv<Ack, End>, End>>>);

static_assert(std::is_same_v<Delegate_seq<DelegatedProto, DelegatedProto, DelegatedProto, End>,
                             Delegate<DelegatedProto, Delegate<DelegatedProto, Delegate<DelegatedProto, End>>>>);

static_assert(std::is_same_v<dual_of_t<Delegate_seq<DelegatedProto, DelegatedProto, End>>,
                             Accept_seq<DelegatedProto, DelegatedProto, End>>);
static_assert(std::is_same_v<dual_of_t<Delegate_seq<DelegatedProto, Send<int, End>>>,
                             Accept_seq<DelegatedProto, Recv<int, End>>>);

static_assert(std::is_same_v<dual_of_t<dual_of_t<Delegate_seq<DelegatedProto, DelegatedProto, End>>>,
                             Delegate_seq<DelegatedProto, DelegatedProto, End>>);

static_assert(is_well_formed_v<Delegate_seq<DelegatedProto, DelegatedProto, End>>);
static_assert(is_well_formed_v<Accept_seq<DelegatedProto, DelegatedProto, End>>);

static_assert(std::is_same_v<Redelegate<DelegatedProto, End>, Accept<DelegatedProto, Delegate<DelegatedProto, End>>>);

static_assert(
    std::is_same_v<dual_of_t<Redelegate<DelegatedProto, End>>, Delegate<DelegatedProto, Accept<DelegatedProto, End>>>);

static_assert(std::is_same_v<dual_of_t<dual_of_t<Redelegate<DelegatedProto, Send<int, End>>>>,
                             Redelegate<DelegatedProto, Send<int, End>>>);

static_assert(is_well_formed_v<Redelegate<DelegatedProto, End>>);

struct AckFixture {};

static_assert(
    std::is_same_v<DelegateWithAck<DelegatedProto, AckFixture, End>, Delegate<DelegatedProto, Recv<AckFixture, End>>>);

static_assert(
    std::is_same_v<AcceptWithAck<DelegatedProto, AckFixture, End>, Accept<DelegatedProto, Send<AckFixture, End>>>);

static_assert(std::is_same_v<dual_of_t<DelegateWithAck<DelegatedProto, AckFixture, End>>,
                             AcceptWithAck<DelegatedProto, AckFixture, End>>);

static_assert(std::is_same_v<dual_of_t<dual_of_t<DelegateWithAck<DelegatedProto, AckFixture, End>>>,
                             DelegateWithAck<DelegatedProto, AckFixture, End>>);

static_assert(is_well_formed_v<DelegateWithAck<DelegatedProto, AckFixture, End>>);
static_assert(is_well_formed_v<AcceptWithAck<DelegatedProto, AckFixture, End>>);

// Function-pointer types are used instead of lambdas.  They are stable at
// namespace scope and do not raise the noexcept warning a lambda draws when
// its body might throw.

namespace transport_concept_test {
struct CarrierRes {};
struct DelegatedRes {};

using ValidDelegateTransport = void (*)(CarrierRes&, DelegatedRes&&);
using ValidAcceptTransport = DelegatedRes (*)(CarrierRes&);
using WrongArityTransport = void (*)(int, int, int);
using WrongReturnTransport = int (*)(CarrierRes&);

static_assert(TransportForDelegate<ValidDelegateTransport, CarrierRes, DelegatedRes>);
static_assert(!TransportForDelegate<ValidAcceptTransport, CarrierRes, DelegatedRes>);
static_assert(!TransportForDelegate<WrongArityTransport, CarrierRes, DelegatedRes>);

static_assert(TransportForAccept<ValidAcceptTransport, CarrierRes, DelegatedRes>);
static_assert(!TransportForAccept<ValidDelegateTransport, CarrierRes, DelegatedRes>);
static_assert(!TransportForAccept<WrongReturnTransport, CarrierRes, DelegatedRes>);
}  // namespace transport_concept_test

struct RecipientTag {};
struct Result {};

using RecvOnlyDelegated = Recv<int, End>;
using CleanCarrierK = Loop<Send<Result, Continue>>;

static_assert(std::is_same_v<delegated_crash_propagation_t<RecvOnlyDelegated, RecipientTag, CleanCarrierK>,
                             Recovers<CleanCarrierK>>);

using SendingDelegated = Send<int, End>;
using NoCrashRecoveryK = Recv<Ack, End>;

static_assert(
    std::is_same_v<delegated_crash_propagation_t<SendingDelegated, RecipientTag, NoCrashRecoveryK>, MustAbort>);

using CrashRecoveringK = Offer<Recv<Ack, End>, Recv<Crash<RecipientTag>, Send<Result, End>>>;

static_assert(std::is_same_v<delegated_crash_propagation_t<SendingDelegated, RecipientTag, CrashRecoveringK>,
                             Recovers<Send<Result, End>>>);

using SenderAnnotatedCrashRecoveringK =
    Offer<Sender<RecipientTag>, Recv<Ack, End>, Recv<Crash<RecipientTag>, Send<Result, End>>>;

static_assert(
    std::is_same_v<delegated_crash_propagation_t<SendingDelegated, RecipientTag, SenderAnnotatedCrashRecoveringK>,
                   Recovers<Send<Result, End>>>);

static_assert(std::is_same_v<delegated_crash_propagation_t<Stop, RecipientTag, CrashRecoveringK>, IllFormed>);

}  // namespace detail::delegate_self_test
#endif  // CRUCIBLE_SESSION_SELF_TESTS

}  // namespace crucible::safety::proto
