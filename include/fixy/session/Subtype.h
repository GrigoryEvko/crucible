#pragma once

// Subtyping over session protocols: a synchronous relation, a bounded
// asynchronous relation, and the payload order both of them read.
//
// T refines U when a process of type T can stand where a process of
// type U is expected.
//
// ── The synchronous relation ──────────────────────────────────────────
//
// is_subtype_sync_v<T, U> is the refinement preorder of the transition
// algebra in foundation/algebra/Transition.h, over the combinators of
// fixy/session/Protocol.h.  It runs on the two type graphs and visits
// each pair of nodes once, so it is O(|T|·|U|) (Udomsrirungruang and
// Yoshida, POPL 2025, section 5).  It is the coinductive relation on
// the unfoldings, so a loop and one unfold of it refine each other.
//
// Per position:
//
//   Send   covariant in the payload.  A narrower payload stands where a
//          wider one is expected.
//   Recv   contravariant in the payload.  A receiver that accepts a
//          wider payload stands where a narrower one is expected.
//   Select the subtype picks from no more branches than the supertype.
//   Offer  the subtype handles no fewer branches than the supertype.
//   Loop   unfolded.  A pair seen a second time holds by assumption.
//   VendorPinned  the same vendor on both sides.
//
// The relation matches a label branch by the word that the handle sends
// for it (fixy/session/Handle.h).  In a keyed choice, where each label
// branch names a label key (a PeerMsg or a Labelled payload), the word is
// the label word, and the branches match by label in any order.  A
// Select of the subtype sends a subset of the labels of the supertype,
// and an Offer of the subtype receives a superset (Gay and Hole, 2005).
// In a positional choice the word is the position, so a Select or an
// Offer with the same branches in another order is another protocol, and
// the relation refuses it.  A keyed choice never refines a positional
// one, because the two put different words on the wire.
//
// A keyed step is the keyed choice of its one branch.  A Send or a Recv
// whose payload names a label key is the Select or the Offer of that one
// label branch, because a message is a choice with one label (Barwell,
// Hou, Yoshida and Zhou, LMCS 2025, Definition 4.9).  The type graph of
// foundation/algebra/Transition.h reads the step so, and both relations
// walk that graph.  A keyed Send refines each keyed Select that has its
// label.  An Offer that receives the label refines a keyed Recv of it.
// The Recv takes the Sender note of the peer that its payload names, so
// only an Offer with that note refines it.  A step whose payload names no
// label is a plain step, and it never pairs with a choice.
//
// A branch that is no label, a crash branch for example, has no word on
// the wire.  The relation matches it by the payload it receives, wherever
// it stands (rule Sub-&, Barwell, Hou, Yoshida and Zhou, LMCS 2025,
// Definition 4.4).  So an Offer of the subtype can add a message branch
// before its crash branches.  The relation compares the Sender note of an
// Offer for equality, so each combinator is reflexive, the Offer with a
// note included.  An operand that is not well-formed is refused, and the
// reason says so.  An empty choice is not well-formed.
//
// One walk gives the verdict and its reason.  subtype_verdict_v names
// the first failed pair, in the order a depth-first walk reaches it,
// and subtype_reason_t turns the verdict into a type.  No second walk
// exists that could disagree with the first.
//
// Exits.  The relation keeps each exit of the supertype: at each pair of
// the walk where U can still end, the pair can still end (fair
// subtyping, Padovani and Zavattaro, TOPLAS 2026).  A Select of the
// subtype that drops the only exit of a loop is refused, with the reason
// loses_termination.  A stream refines a stream, because the supertype
// never ends either.
//
// The relation up to exits is closed under duality: when T refines U,
// the dual of U refines the dual of T.  The payload variance flips with
// the shape, the branch rule of Select mirrors the rule of Offer, and
// the vendor value is invariant.  Exit preservation is not closed under
// duality, so CompatibleClient and CompatibleServer below ask for
// refinement in both directions.
//
// ── The asynchronous relation ─────────────────────────────────────────
//
// Precise asynchronous subtyping is undecidable, also for two parties
// (Bravetti, Carbone and Zavattaro, 2017; Lange and Yoshida, 2017).
// is_subtype_async_v<T, U, Channel> is a sound check that can fail to
// prove a true pair.  A pair it cannot prove is refused.
//
// The capacity of the check is a property of the channel, never a number
// that the caller states.  Channel is the type of the channel end that
// the session runs on, and its constant member `channel_capacity` states
// how many messages the channel holds in one direction.  A check at a
// capacity that the channel does not have is not sound: a pair that holds
// at capacity 4 can deadlock on a channel of capacity 1.  So the one
// mint that admits an asynchronous pair, mint_forked_async_channel in
// fixy/session/AsyncChannel.h, runs the check at the channel type of the
// Resources that the two sides use.  The statement of the channel type is
// the contract: a channel whose buffer holds fewer messages than its type
// states breaks it, as a transport that drops a message does.
//
// The check is the algorithm of Cutner, Yoshida and Vassor, Rumpsteak
// (PPoPP 2022, section 3.2, rules oi, oo, ii, io, sub, asm, μL and μR),
// for one peer.  With one peer, an output of the subtype can move ahead
// of inputs, and nothing else can move.  The transitivity rule is not
// used, which keeps the check sound and makes it less complete.  The
// check keeps the exits of the supertype on the derivation that it
// proves, as the synchronous relation does on its product.
//
// The capacity of the channel bounds two things:
//
//   1. The number of outputs the subtype sends ahead of the supertype,
//      and the number of messages the peer sends before the subtype
//      receives them.  Each is a count of messages in one direction of
//      the channel.  An anticipation beyond the capacity fills the
//      buffer, both sides can then wait on a full buffer, and the
//      session deadlocks.  Rumpsteak assumes unbounded buffers and has
//      no such bound.
//   2. The number of unfolds per side, which is the capacity plus one.
//      The first unfold records the assumption that each later visit of
//      the loop discharges.
//
// The search also has a fixed fuel, stated at search_fuel.  A pair whose
// proof needs more work is refused as not proven, well before the build
// reaches its constexpr operation limit.  The synchronous relation runs
// first, and a pair that it holds skips the search.
//
// The check also runs on the duals, T := dual(U) and U := dual(T), and
// both runs must hold, so the relation is closed under duality by
// construction (Padovani and Zavattaro, TOPLAS 2026, page 3).  The
// synchronous relation is a subset: a pair it holds needs no
// anticipation, so the asynchronous relation holds it at every
// capacity.
//
// The check refuses, as not proven, a protocol with a wrapper below the
// top, and a protocol with a payload that a payload registration marks
// as no label.
//
// ── The payload order ─────────────────────────────────────────────────
//
// The payload order is the reflexive relation that the axioms in
// `fixy::session::payload_axioms` generate:
//
//   Refined<P, T>              ⩽  T
//   Refinement<P, T, S>        ⩽  Refinement<Q, T, S>   when P implies Q
//                                                        and P and Q are
//                                                        defined on T
//   Tagged<T, V>              ⩽  T                     for a tag V in
//                                                        droppable_tags
//   NumericalTier<Tight, T>    ⩽  NumericalTier<Loose, T>
//   PeerMsg<R, L, T>           ⩽  PeerMsg<R, L, U>      when T ⩽ U
//   DelegatedSession<P, Res, Pol, PS>
//                              ⩽  DelegatedSession<Q, Res, Pol, PS>
//                                                       when Q refines P
//   ContentAddressed<T>        ⩽  T  and  T  ⩽  ContentAddressed<T>,
//                                 at any depth of the marker
//
// The content-addressed marker changes no content, so the two sides are
// subsorts of each other.  The axiom strips every layer of the marker
// from both sides and compares what is left.  It relates a payload to
// its own marked form only: it does not reach across the marker to
// another axiom, because nothing claims that the two compose.
//
// A delegated endpoint is contravariant in its protocol (Gay and Hole,
// 2005, for channel types).  The recipient runs code of the protocol
// that its type names, and that code takes over the obligation of the
// endpoint it receives.  Code of protocol Q can take over an obligation
// of protocol P when Q refines P.  So a send of an endpoint at P stands
// where a send of an endpoint at Q is expected.
//
// A seal closes the axioms.  An axiom that a later header adds stops the
// build at the next query of the order, so each translation unit that
// compiles reads the same order.
//
// The implication is the relation of fixy/Refined.h, which is transitive:
// P implies Q when a chain of admitted steps joins them.  A predicate
// implies itself here, because the order is reflexive.
//
// The absences are the discipline:
//
//   A SealedRefined does not drop its predicate, because it has no door
//     that returns the value.
//   Tagged<T, External>, Tagged<T, FromUser> and Tagged<T, FromPytorch>
//     do not drop to T.  That would carry untrusted input into a
//     position that assumes validation.  Such a tag is retagged after a
//     check, never dropped.
//   A trust tag, an access tag and a version tag do not drop to T.
//     Each carries more than T, and the call site must show the loss.
//   Linear<T> and Secret<T> do not drop to T.
//   T does not rise to Tagged<T, V> or Refined<P, T>.  A value cannot
//     acquire a guarantee that nobody established.
//
// Each axiom keeps the representation of the value: the wrapper is an
// empty base or an empty member, so a subtype payload has the size and
// the bytes of its supertype payload.  The test suite checks this for
// each axiom.

#include <fixy/Bands.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <fixy/session/ContentAddressed.h>
#include <fixy/session/Protocol.h>
#include <foundation/algebra/Transition.h>
#include <foundation/algebra/lattices/ToleranceLattice.h>
#include <foundation/contracts/Armed.h>
#include <foundation/diag/FailClosed.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace fixy::session {

// ── The payload order ────────────────────────────────────────────────

// The tags whose value drops to the bare payload.  Each one records
// that the value was checked, computed here, or read from a store the
// process trusts.  The relation is closed: a tag drops only when an
// edge to void names it here.
namespace droppable_tags {
inline constexpr ::foundation::fail_closed::edge<::fixy::tags::source::Sanitized, void> sanitized{};
inline constexpr ::foundation::fail_closed::edge<::fixy::tags::source::FromInternal, void> from_internal{};
inline constexpr ::foundation::fail_closed::edge<::fixy::tags::source::FromConfig, void> from_config{};
inline constexpr ::foundation::fail_closed::edge<::fixy::tags::source::FromDb, void> from_db{};
inline constexpr ::foundation::fail_closed::edge<::fixy::tags::source::Durable, void> durable{};
inline constexpr ::foundation::fail_closed::edge<::fixy::tags::source::Computed, void> computed{};
inline constexpr ::foundation::fail_closed::edge<::fixy::tags::vessel_trust::Validated, void> validated{};
// Every read counts the members against this seal, so a member that
// another file adds stops the build rather than widening the relation.
inline constexpr ::foundation::fail_closed::seal sealed{.members = 7};
}  // namespace droppable_tags

namespace detail::subtype {

// The synchronous verdict on one pair, defined below.  The payload order
// reads it for a delegated endpoint, whose protocol is a smaller type
// than the protocol that sends it, so the recursion ends.
template <typename Sub, typename Super>
consteval ::foundation::algebra::transition::verdict sync_verdict();

}  // namespace detail::subtype

namespace detail::payload_order {

template <typename T, typename U>
inline constexpr bool delegation_weakens_v = false;
template <typename P, typename Q, typename Resource, typename Policy, typename PS>
inline constexpr bool delegation_weakens_v<DelegatedSession<P, Resource, Policy, PS>,
                                           DelegatedSession<Q, Resource, Policy, PS>> =
    ::fixy::session::detail::subtype::sync_verdict<Q, P>().holds;

template <typename T>
struct refinement_parts {
    static constexpr bool is_refinement = false;
    static constexpr bool is_sealed = false;
    using value_type = T;
};
template <auto P, typename X, bool Sealed>
struct refinement_parts<::fixy::Refinement<P, X, Sealed>> {
    static constexpr bool is_refinement = true;
    static constexpr bool is_sealed = Sealed;
    using value_type = X;
};

template <typename T>
struct tagged_parts {
    static constexpr bool is_tagged = false;
    using value_type = T;
    using tag = void;
};
template <typename X, typename V>
struct tagged_parts<::fixy::Tagged<X, V>> {
    static constexpr bool is_tagged = true;
    using value_type = X;
    using tag = V;
};

template <typename T>
inline constexpr bool refinement_drops_v = refinement_parts<T>::is_refinement && !refinement_parts<T>::is_sealed;

template <typename T>
using refinement_value_t = typename refinement_parts<T>::value_type;

template <typename T, typename U>
inline constexpr bool refinement_weakens_v = false;
// The relation of fixy/Refined.h reads its admitted steps where that
// header defines them, so an edge that a later header adds does not
// change this order.  A step of that relation holds on a value type only
// where its two predicates are defined, so each predicate must evaluate
// the value type here: a trusted refinement whose predicate the value
// type cannot evaluate, such as bounded_below<256> over a std::uint8_t,
// weakens to nothing.
template <auto P, auto Q, typename X, bool Sealed>
inline constexpr bool refinement_weakens_v<::fixy::Refinement<P, X, Sealed>, ::fixy::Refinement<Q, X, Sealed>> =
    std::is_same_v<::fixy::refined::predicate_t<P>, ::fixy::refined::predicate_t<Q>>
    || (::fixy::PredicateInvocableOn<P, X> && ::fixy::PredicateInvocableOn<Q, X>
        && ::fixy::refined::implies_types<::fixy::refined::predicate_t<P>, ::fixy::refined::predicate_t<Q>>());

template <typename T>
inline constexpr bool tagged_drops_v =
    tagged_parts<T>::is_tagged
    && ::foundation::fail_closed::admits<^^::fixy::session::droppable_tags, typename tagged_parts<T>::tag, void>();

template <typename T>
using tagged_value_t = typename tagged_parts<T>::value_type;

template <typename T, typename U>
inline constexpr bool tier_weakens_v = false;
template <::foundation::algebra::lattices::Tolerance Tight, ::foundation::algebra::lattices::Tolerance Loose,
          typename X>
inline constexpr bool tier_weakens_v<::fixy::NumericalTier<Tight, X>, ::fixy::NumericalTier<Loose, X>> =
    ::foundation::algebra::lattices::ToleranceLattice::leq(Loose, Tight);

}  // namespace detail::payload_order

namespace payload_axioms {

inline constexpr ::foundation::algebra::transition::subsort_axiom refinement_drops{
    .drops = ^^detail::payload_order::refinement_drops_v, .inner = ^^detail::payload_order::refinement_value_t};

inline constexpr ::foundation::algebra::transition::subsort_axiom refinement_weakens{
    .weakens = ^^detail::payload_order::refinement_weakens_v};

inline constexpr ::foundation::algebra::transition::subsort_axiom tagged_drops{
    .drops = ^^detail::payload_order::tagged_drops_v, .inner = ^^detail::payload_order::tagged_value_t};

inline constexpr ::foundation::algebra::transition::subsort_axiom tier_weakens{
    .weakens = ^^detail::payload_order::tier_weakens_v};

// A message is below another message when the peers are the same, the
// labels are the same, and the payload is below in the payload order.
// Bit 2 marks the position of Payload.
inline constexpr ::foundation::algebra::transition::subsort_axiom peer_message{.congruence = ^^PeerMsg,
                                                                              .covariant = 0b100};

// A delegated endpoint is below another when its protocol is refined by
// the protocol of the other, and the Resource, the policy and the set are
// the same.
inline constexpr ::foundation::algebra::transition::subsort_axiom delegation_contravariant{
    .weakens = ^^detail::payload_order::delegation_weakens_v};

// A payload and its content-addressed form are below each other, at any
// depth of the marker.
inline constexpr ::foundation::algebra::transition::subsort_axiom content_addressed{
    .weakens = ^^::fixy::session::content_addressed_equivalent_v};

inline constexpr ::foundation::algebra::transition::seal axiom_seal{
    .kind = ^^::foundation::algebra::transition::subsort_axiom, .count = 7};

}  // namespace payload_axioms

// The seal counts every axiom of the order.  An axiom that stands before
// this header, in a namespace that a different header opened first,
// makes the count differ here.
static_assert(::foundation::algebra::transition::read_seal(^^::fixy::session::payload_axioms,
                                                           ^^::foundation::algebra::transition::subsort_axiom)
                      .fault
                  == ::foundation::algebra::transition::seal_fault::none,
              "fixy::session::diagnostic [Subtype_Axiom_Outside_Seal]: fixy::session::payload_axioms holds an axiom "
              "that its seal does not count.  Every axiom of the payload order stands in fixy/session/Subtype.h.");

template <typename Sub, typename Super>
inline constexpr bool is_payload_subsort_v =
    ::foundation::algebra::transition::subsorts(^^::fixy::session::payload_axioms, ^^Sub, ^^Super);

// ── The synchronous relation ─────────────────────────────────────────

namespace detail::subtype {

inline constexpr std::string_view unregistered_prefix = "fixy::session::diagnostic [Subtype_Unregistered_Combinator]: ";
inline constexpr std::string_view incoherent_prefix = "fixy::session::diagnostic [Subtype_Incoherent_Registration]: ";

// Both spines must hold registered combinators only.  The relation
// reads every node, so a combinator it does not know stops the build.
template <typename P>
consteval void require_registered_spine() {
    static_assert(::foundation::algebra::transition::first_unregistered(protocol_registry, ^^P) == std::meta::info{},
                  ::foundation::algebra::transition::unregistered_message(
                      unregistered_prefix,
                      ::foundation::algebra::transition::first_unregistered(protocol_registry, ^^P)));
    static_assert(::foundation::algebra::transition::first_incoherent(protocol_registry, ^^P).reason
                      == ::foundation::algebra::transition::incoherence::none,
                  ::foundation::algebra::transition::incoherent_message(
                      incoherent_prefix, ::foundation::algebra::transition::first_incoherent(protocol_registry, ^^P)));
}

template <typename Sub, typename Super>
consteval ::foundation::algebra::transition::verdict sync_verdict() {
    require_registered_spine<Sub>();
    require_registered_spine<Super>();
    if (!is_well_formed_v<Sub>) {
        return {false, ::foundation::algebra::transition::mismatch::ill_formed, ^^Sub, {}};
    }
    if (!is_well_formed_v<Super>) {
        return {false, ::foundation::algebra::transition::mismatch::ill_formed, {}, ^^Super};
    }
    return ::foundation::algebra::transition::refines(protocol_registry, ^^::fixy::session::payload_axioms, ^^Sub,
                                                      ^^Super);
}

}  // namespace detail::subtype

template <typename Sub, typename Super>
inline constexpr ::foundation::algebra::transition::verdict subtype_verdict_v =
    detail::subtype::sync_verdict<Sub, Super>();

template <typename Sub, typename Super>
inline constexpr ::foundation::algebra::transition::mismatch subtype_mismatch_v = subtype_verdict_v<Sub, Super>.reason;

template <typename Sub, typename Super>
inline constexpr bool is_subtype_sync_v = subtype_verdict_v<Sub, Super>.holds;

template <typename Sub, typename Super>
concept SubtypeSync = is_subtype_sync_v<Sub, Super>;

// The question "does Sub refine Super", as one type, so the predicate
// below takes one argument and can hold an armed cell.
template <typename Sub, typename Super>
struct SubtypeQuery {};

template <typename Q>
struct is_sync_subtype : std::false_type {};
template <typename Sub, typename Super>
struct is_sync_subtype<SubtypeQuery<Sub, Super>> : std::bool_constant<is_subtype_sync_v<Sub, Super>> {};

// ── The reason, as a type ────────────────────────────────────────────

struct SubtypeOk {};

// Lhs and Rhs are the failed pair.  For a payload they are the two
// payloads, read as "the payload supplied is not below the payload
// expected", so for a Recv the order is the reverse of the operands.
// An operand that is not well-formed is Lhs or Rhs, and the other one
// is void.
template <::foundation::algebra::transition::mismatch Reason, typename Lhs, typename Rhs>
struct SubtypeRejection {
    static constexpr ::foundation::algebra::transition::mismatch reason = Reason;
    using lhs = Lhs;
    using rhs = Rhs;
    static constexpr std::string_view description = ::foundation::algebra::transition::mismatch_name(Reason);
};

namespace detail::subtype {

[[nodiscard]] consteval std::meta::info or_void(std::meta::info type) {
    return type == std::meta::info{} ? ^^void : type;
}

template <typename Sub, typename Super>
struct reason_of {
    static constexpr ::foundation::algebra::transition::verdict found = subtype_verdict_v<Sub, Super>;
    using type = std::conditional_t<found.holds, SubtypeOk,
                                    SubtypeRejection<found.reason, typename[:or_void(found.sub):],
                                                     typename[:or_void(found.super):]>>;
};

[[nodiscard]] consteval std::string_view mismatch_token(::foundation::algebra::transition::mismatch reason) {
    using ::foundation::algebra::transition::mismatch;
    switch (reason) {
        case mismatch::none:
            return "Subtype_Holds";
        case mismatch::shape:
            return "Subtype_Shape";
        case mismatch::payload:
            return "Subtype_Payload";
        case mismatch::branch_count:
            return "Subtype_BranchCount";
        case mismatch::value:
            return "Subtype_Vendor";
        case mismatch::annotation:
            return "Subtype_Annotation";
        case mismatch::non_label_branch:
            return "Subtype_NonLabelBranch";
        case mismatch::pure_non_label_choice:
            return "Subtype_PureNonLabelChoice";
        case mismatch::unguarded:
            return "Subtype_Unguarded";
        case mismatch::unregistered:
            return "Subtype_Unregistered_Combinator";
        case mismatch::ill_formed:
            return "Subtype_IllFormed";
        case mismatch::missing_non_label_branch:
            return "Subtype_MissingNonLabelBranch";
        case mismatch::loses_termination:
            return "Subtype_LosesTermination";
        case mismatch::label_set:
            return "Subtype_LabelSet";
        case mismatch::label_discipline:
            return "Subtype_LabelDiscipline";
        case mismatch::label_word_clash:
            return "Subtype_LabelWordClash";
        default:
            break;
    }
    return "Subtype_Unknown";
}

// The text of a refusal: the class of the failure, then the failed pair.
template <typename Sub, typename Super>
consteval std::string_view refusal_message(std::string_view site) {
    const ::foundation::algebra::transition::verdict found = subtype_verdict_v<Sub, Super>;
    std::string text = "fixy::session::diagnostic [";
    text += mismatch_token(found.reason);
    text += "]: ";
    text += site;
    text += ": the subtype does not refine the supertype, because ";
    text += ::foundation::algebra::transition::mismatch_name(found.reason);
    text += ".  The failed pair is ";
    text += found.sub == std::meta::info{} ? std::string_view{"(none)"} : std::meta::display_string_of(found.sub);
    text += " against ";
    text += found.super == std::meta::info{} ? std::string_view{"(none)"} : std::meta::display_string_of(found.super);
    text += ".";
    return std::define_static_string(text);
}

}  // namespace detail::subtype

template <typename Sub, typename Super>
using subtype_reason_t = typename detail::subtype::reason_of<Sub, Super>::type;

// ── Derived relations ────────────────────────────────────────────────
//
// Two protocols that refine each other describe the same traces and
// are interchangeable in every context.  A strict subtype is a subtype
// that is not equivalent, so a refinement that changed nothing reads as
// false.  A client is compatible with a server when the client refines
// the dual of the server, because the peer of a server is its dual.

template <typename Sub, typename Super>
inline constexpr bool equivalent_sync_v = is_subtype_sync_v<Sub, Super> && is_subtype_sync_v<Super, Sub>;

template <typename Sub, typename Super>
concept EquivalentSync = equivalent_sync_v<Sub, Super>;

template <typename Sub, typename Super>
inline constexpr bool is_strict_subtype_sync_v = is_subtype_sync_v<Sub, Super> && !is_subtype_sync_v<Super, Sub>;

template <typename Sub, typename Super>
concept StrictSubtypeSync = is_strict_subtype_sync_v<Sub, Super>;

namespace detail::subtype {

template <typename... Ts>
consteval bool chain_holds() {
    if constexpr (sizeof...(Ts) < 2) {
        return true;
    } else {
        return []<typename First, typename Second, typename... Rest>(std::type_identity<First>,
                                                                     std::type_identity<Second>,
                                                                     std::type_identity<Rest>...) {
            return is_subtype_sync_v<First, Second> && chain_holds<Second, Rest...>();
        }(std::type_identity<Ts>{}...);
    }
}

}  // namespace detail::subtype

// Each adjacent pair refines.  The relation is transitive when the
// payload order is, so the first element then refines the last.
template <typename... Ts>
inline constexpr bool subtype_chain_v = detail::subtype::chain_holds<Ts...>();

// A client and a server are compatible when each refines the dual of the
// other.  Refinement keeps the exits of the supertype, and that condition
// is not closed under duality: a loop that never picks its exit does not
// refine the loop that can, but the dual of the second refines the dual
// of the first, because a receiver that never ends keeps no exit.  One
// direction alone would let a check from one side admit a pair that the
// check from the other side refuses.  Both directions make compatibility
// one symmetric relation, so CompatibleClient<C, S> and
// CompatibleServer<S, C> always agree.
template <typename ClientProto, typename ServerProto>
concept CompatibleClient = is_subtype_sync_v<ClientProto, dual_of_t<ServerProto>>
                           && is_subtype_sync_v<ServerProto, dual_of_t<ClientProto>>;

template <typename ServerProto, typename ClientProto>
concept CompatibleServer = CompatibleClient<ClientProto, ServerProto>;

template <typename Sub, typename Super>
consteval void assert_subtype_sync() noexcept {
    static_assert(is_subtype_sync_v<Sub, Super>, detail::subtype::refusal_message<Sub, Super>("assert_subtype_sync"));
}

template <typename Sub, typename Super>
consteval void assert_equivalent_sync() noexcept {
    static_assert(is_subtype_sync_v<Sub, Super>,
                  detail::subtype::refusal_message<Sub, Super>("assert_equivalent_sync, forward direction"));
    static_assert(is_subtype_sync_v<Super, Sub>,
                  detail::subtype::refusal_message<Super, Sub>("assert_equivalent_sync, reverse direction"));
}

template <typename ClientProto, typename ServerProto>
consteval void assert_compatible_client() noexcept {
    static_assert(is_subtype_sync_v<ClientProto, dual_of_t<ServerProto>>,
                  detail::subtype::refusal_message<ClientProto, dual_of_t<ServerProto>>(
                      "assert_compatible_client: the client must refine the dual of the server"));
    static_assert(is_subtype_sync_v<ServerProto, dual_of_t<ClientProto>>,
                  detail::subtype::refusal_message<ServerProto, dual_of_t<ClientProto>>(
                      "assert_compatible_client: the server must refine the dual of the client"));
}

template <typename ServerProto, typename ClientProto>
consteval void assert_compatible_server() noexcept {
    static_assert(is_subtype_sync_v<ServerProto, dual_of_t<ClientProto>>,
                  detail::subtype::refusal_message<ServerProto, dual_of_t<ClientProto>>(
                      "assert_compatible_server: the server must refine the dual of the client"));
    static_assert(is_subtype_sync_v<ClientProto, dual_of_t<ServerProto>>,
                  detail::subtype::refusal_message<ClientProto, dual_of_t<ServerProto>>(
                      "assert_compatible_server: the client must refine the dual of the server"));
}

// The older protocol comes first, which reads as the version ladder.
// The relation runs from the newer protocol to the older one.
template <typename OldProto, typename NewProto>
consteval void check_protocol_evolution() noexcept {
    static_assert(is_subtype_sync_v<NewProto, OldProto>,
                  detail::subtype::refusal_message<NewProto, OldProto>(
                      "check_protocol_evolution: the new protocol must refine the old one.  A valid "
                      "refinement narrows a Select, widens an Offer, or narrows a Send payload or widens a "
                      "Recv payload in the payload order"));
}

// ── The asynchronous relation ────────────────────────────────────────

namespace detail::async {

// A label action names its branch by the word that the handle sends: the
// label word and the key in a keyed choice, and the position in a
// positional one.
struct action {
    bool is_output = false;
    bool is_label = false;
    bool is_keyed = false;
    std::uint64_t label = 0;
    std::meta::info key{};
    std::meta::info payload{};
    std::meta::info note{};
};

struct move {
    action act{};
    std::size_t next = ::foundation::algebra::transition::npos;
};

struct assumption {
    std::vector<action> sub_prefix{};
    std::size_t sub_node = ::foundation::algebra::transition::npos;
    std::vector<action> super_prefix{};
    std::size_t super_node = ::foundation::algebra::transition::npos;
    std::size_t rho_length = 0;
    std::size_t config = ::foundation::algebra::transition::npos;
};

// The derivation of a proof, as a graph.  Each call of prove that holds
// is a configuration: the node of the supertype it stands at, and true
// when rule end closes it.  An edge leads from a configuration to each
// configuration that its rule proves, and rule asm leads back to the
// assumption it uses.  A path to a configuration that rule end closes is
// a run in which the subtype and the peer of the supertype both end.
struct derivation {
    std::vector<std::size_t> super_node{};
    std::vector<std::uint8_t> is_end{};
    std::vector<std::size_t> edge_from{};
    std::vector<std::size_t> edge_to{};
};

// The length of a derivation, so that a failed attempt can drop what it
// recorded.
struct derivation_mark {
    std::size_t configs = 0;
    std::size_t edges = 0;
};

// The fuel of one direction of the search.  Each step spends units in
// proportion to the work it does: the prefixes it copies, the
// assumptions it scans and the actions it reads back.  A search that
// runs out has not proven the pair, and the pair is refused.  The
// amount keeps each direction well inside the constexpr operation limit
// of the build (-fconstexpr-ops-limit=100000000 in CMakeLists.txt), so
// a hard pair is refused with an answer and never stops the build.  On
// the hardest pair of the differential corpus, one unit costs about 220
// operations, and the limit falls between 370,000 and 524,288 units, so
// this amount is about one seventh of the limit.  No pair of the corpus
// holds with twice this amount and fails with it.
inline constexpr std::size_t search_fuel = std::size_t{1} << 16;

struct search {
    ::foundation::algebra::transition::graph_view sub{};
    ::foundation::algebra::transition::graph_view super{};
    std::meta::info axioms{};
    std::size_t capacity = 0;
    std::size_t fuel = search_fuel;
    std::vector<action> rho{};
    std::vector<assumption> sigma{};
    derivation proof{};
};

// The current length of the derivation.
[[nodiscard]] consteval derivation_mark mark_of(const search& state) {
    return {state.proof.super_node.size(), state.proof.edge_from.size()};
}

// Drops what a failed attempt recorded after `mark`.
consteval void drop_after(search& state, derivation_mark mark) {
    state.proof.super_node.resize(mark.configs);
    state.proof.is_end.resize(mark.configs);
    state.proof.edge_from.resize(mark.edges);
    state.proof.edge_to.resize(mark.edges);
}

// Records the edge from one configuration to another.  A configuration
// with no parent is the root, and no edge leads to it.
consteval void add_edge(search& state, std::size_t from, std::size_t to) {
    if (from == ::foundation::algebra::transition::npos) return;
    state.proof.edge_from.push_back(from);
    state.proof.edge_to.push_back(to);
}

// Spends `units` of fuel.  False when the fuel does not cover them, and
// then the fuel is empty and every later step fails too.
[[nodiscard]] consteval bool spend(search& state, std::size_t units) {
    if (units > state.fuel) {
        state.fuel = 0;
        return false;
    }
    state.fuel -= units;
    return true;
}

[[nodiscard]] consteval bool same_action(const action& left, const action& right) {
    return left.is_output == right.is_output && left.is_label == right.is_label && left.is_keyed == right.is_keyed
           && left.label == right.label && left.key == right.key && left.payload == right.payload
           && left.note == right.note;
}

[[nodiscard]] consteval bool same_prefix(const std::vector<action>& left, const std::vector<action>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!same_action(left[index], right[index])) return false;
    }
    return true;
}

// An action of the subtype matches an action of the supertype when both
// have the same direction and the same label, or when the payloads are
// in the payload order for that direction.  Two labels are the same when
// both are keyed, with one word and one key, or both are positional, with
// one position.
[[nodiscard]] consteval bool matches(std::meta::info axioms, const action& sub, const action& super) {
    if (sub.is_output != super.is_output || sub.is_label != super.is_label) return false;
    if (sub.is_label) {
        return sub.is_keyed == super.is_keyed && sub.label == super.label && sub.key == super.key
               && sub.note == super.note;
    }
    return sub.is_output ? ::foundation::algebra::transition::subsorts(axioms, sub.payload, super.payload)
                         : ::foundation::algebra::transition::subsorts(axioms, super.payload, sub.payload);
}

// Prefix reduction for one peer.  An input at the head of the subtype
// prefix matches an input at the head of the supertype prefix (rule →i).
// An output at the head of the subtype prefix matches the first output
// of the supertype prefix after its inputs (rules →o and →B).  Nothing
// else moves.
consteval void reduce(std::meta::info axioms, std::vector<action>& sub_prefix, std::vector<action>& super_prefix) {
    while (!sub_prefix.empty()) {
        const action head = sub_prefix.front();
        std::size_t partner = ::foundation::algebra::transition::npos;
        if (head.is_output) {
            for (std::size_t index = 0; index < super_prefix.size(); ++index) {
                if (super_prefix[index].is_output) {
                    partner = index;
                    break;
                }
            }
        } else if (!super_prefix.empty() && !super_prefix.front().is_output) {
            partner = 0;
        }
        if (partner == ::foundation::algebra::transition::npos || !matches(axioms, head, super_prefix[partner])) return;
        sub_prefix.erase(sub_prefix.begin());
        super_prefix.erase(super_prefix.begin() + static_cast<std::ptrdiff_t>(partner));
    }
}

// The subtype prefix holds the outputs the subtype sent ahead, and the
// supertype prefix holds the inputs the peer sent before the subtype
// received them.  Each is a count of messages in one buffer.
[[nodiscard]] consteval bool fits(std::size_t capacity, const std::vector<action>& sub_prefix,
                                  const std::vector<action>& super_prefix) {
    std::size_t ahead = 0;
    for (const action& act : sub_prefix) ahead += act.is_output ? 1 : 0;
    std::size_t queued = 0;
    for (const action& act : super_prefix) queued += act.is_output ? 0 : 1;
    return ahead <= capacity && queued <= capacity;
}

// act(ρ') ⊇ act(π'): since the assumption, the subtype did an action of
// each direction that the supertype prefix still holds.
[[nodiscard]] consteval bool covers(const std::vector<action>& rho, std::size_t from,
                                    const std::vector<action>& super_prefix) {
    bool needs_output = false;
    bool needs_input = false;
    for (const action& act : super_prefix) {
        needs_output = needs_output || act.is_output;
        needs_input = needs_input || !act.is_output;
    }
    bool has_output = false;
    bool has_input = false;
    for (std::size_t index = from; index < rho.size(); ++index) {
        has_output = has_output || rho[index].is_output;
        has_input = has_input || !rho[index].is_output;
    }
    return (!needs_output || has_output) && (!needs_input || has_input);
}

[[nodiscard]] consteval bool is_action_node(const ::foundation::algebra::transition::graph_node& node) {
    return node.entry.kind == ::foundation::algebra::transition::shape_kind::step
           || node.entry.kind == ::foundation::algebra::transition::shape_kind::choice;
}

[[nodiscard]] consteval std::vector<move> moves_of(const ::foundation::algebra::transition::graph_view& graph,
                                                   std::size_t index) {
    const ::foundation::algebra::transition::graph_node& node = graph.nodes[index];
    const bool is_output = node.entry.direction == ::foundation::algebra::transition::polarity::output;
    std::vector<move> result;
    if (node.entry.kind == ::foundation::algebra::transition::shape_kind::step) {
        result.push_back(move{action{is_output, false, false, 0, {}, node.payload, {}}, node.next});
        return result;
    }
    for (std::size_t branch = 0; branch < node.child_count; ++branch) {
        const std::size_t child = graph.children[node.first_child + branch];
        const ::foundation::algebra::transition::graph_node& head = graph.nodes[child];
        const std::uint64_t label = node.is_keyed ? head.label_word : static_cast<std::uint64_t>(branch);
        const std::meta::info key = node.is_keyed ? head.label_key : std::meta::info{};
        result.push_back(move{action{is_output, true, node.is_keyed, label, key, {}, node.annotation}, child});
    }
    return result;
}

// A back node leads to its binder.  The binder itself is not entered
// here, because the unfold is a rule of its own.
[[nodiscard]] consteval std::size_t to_binder(const ::foundation::algebra::transition::graph_view& graph,
                                              std::size_t index) {
    if (index == ::foundation::algebra::transition::npos) return index;
    if (graph.nodes[index].entry.kind == ::foundation::algebra::transition::shape_kind::back) {
        return graph.nodes[index].next;
    }
    return index;
}

consteval bool prove(search& state, std::vector<action> sub_prefix, std::size_t sub_index, std::size_t sub_bound,
                     std::vector<action> super_prefix, std::size_t super_index, std::size_t super_bound,
                     std::size_t parent);

consteval bool exchange(search& state, const std::vector<action>& sub_prefix, std::size_t sub_index,
                        std::size_t sub_bound, const std::vector<action>& super_prefix, std::size_t super_index,
                        std::size_t super_bound, std::size_t here) {
    const std::vector<move> own = moves_of(state.sub, sub_index);
    const std::vector<move> other = moves_of(state.super, super_index);
    const auto attempt = [&](const move& mine, const move& theirs) {
        if (!spend(state, 2 + sub_prefix.size() + super_prefix.size())) return false;
        std::vector<action> next_sub = sub_prefix;
        std::vector<action> next_super = super_prefix;
        next_sub.push_back(mine.act);
        next_super.push_back(theirs.act);
        reduce(state.axioms, next_sub, next_super);
        if (!fits(state.capacity, next_sub, next_super)) return false;
        const derivation_mark mark = mark_of(state);
        state.rho.push_back(mine.act);
        const bool holds = prove(state, next_sub, mine.next, sub_bound, next_super, theirs.next, super_bound, here);
        state.rho.pop_back();
        if (!holds) drop_after(state, mark);
        return holds;
    };
    const bool sub_sends = state.sub.nodes[sub_index].entry.direction
                           == ::foundation::algebra::transition::polarity::output;
    const bool super_sends = state.super.nodes[super_index].entry.direction
                             == ::foundation::algebra::transition::polarity::output;
    if (sub_sends && !super_sends) {
        // Rule oi: every output of the subtype against every input of
        // the supertype.
        for (const move& mine : own) {
            for (const move& theirs : other) {
                if (!attempt(mine, theirs)) return false;
            }
        }
        return true;
    }
    if (sub_sends && super_sends) {
        // Rule oo: each output of the subtype against some output of the
        // supertype.
        for (const move& mine : own) {
            bool found = false;
            for (const move& theirs : other) {
                if (attempt(mine, theirs)) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        return true;
    }
    if (!sub_sends && !super_sends) {
        // Rule ii: each input of the supertype against some input of the
        // subtype.
        for (const move& theirs : other) {
            bool found = false;
            for (const move& mine : own) {
                if (attempt(mine, theirs)) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        return true;
    }
    // Rule io: some input of the subtype against some output of the
    // supertype.
    for (const move& mine : own) {
        for (const move& theirs : other) {
            if (attempt(mine, theirs)) return true;
        }
    }
    return false;
}

consteval bool prove(search& state, std::vector<action> sub_prefix, std::size_t sub_index, std::size_t sub_bound,
                     std::vector<action> super_prefix, std::size_t super_index, std::size_t super_bound,
                     std::size_t parent) {
    using ::foundation::algebra::transition::shape_kind;
    const std::size_t prefix_length = sub_prefix.size() + super_prefix.size();
    if (!spend(state, 1 + prefix_length + state.rho.size() + state.sigma.size() * (1 + prefix_length))) return false;
    sub_index = to_binder(state.sub, sub_index);
    super_index = to_binder(state.super, super_index);
    if (sub_index == ::foundation::algebra::transition::npos || super_index == ::foundation::algebra::transition::npos) {
        return false;
    }
    reduce(state.axioms, sub_prefix, super_prefix);
    const ::foundation::algebra::transition::graph_node& own = state.sub.nodes[sub_index];
    const ::foundation::algebra::transition::graph_node& other = state.super.nodes[super_index];
    if (own.entry.kind == shape_kind::wrapper || other.entry.kind == shape_kind::wrapper) return false;
    // This call is a configuration of the derivation.  A caller whose
    // attempt fails drops it again.
    const std::size_t here = state.proof.super_node.size();
    state.proof.super_node.push_back(super_index);
    state.proof.is_end.push_back(0);
    add_edge(state, parent, here);
    // Rule end.
    if (sub_prefix.empty() && super_prefix.empty() && own.entry.kind == shape_kind::terminal
        && other.entry.kind == shape_kind::terminal) {
        if (own.entry.shape != other.entry.shape) return false;
        state.proof.is_end[here] = 1;
        return true;
    }
    // Rule asm.
    for (const assumption& earlier : state.sigma) {
        if (earlier.sub_node == sub_index && earlier.super_node == super_index
            && same_prefix(earlier.sub_prefix, sub_prefix) && same_prefix(earlier.super_prefix, super_prefix)
            && covers(state.rho, earlier.rho_length, super_prefix)) {
            add_edge(state, here, earlier.config);
            return true;
        }
    }
    // Rules oi, oo, ii and io.
    if (is_action_node(own) && is_action_node(other)) {
        return exchange(state, sub_prefix, sub_index, sub_bound, super_prefix, super_index, super_bound, here);
    }
    // Rules μL and μR.
    if (own.entry.kind == shape_kind::binder && sub_bound > 0) {
        const derivation_mark mark = mark_of(state);
        state.sigma.push_back(assumption{sub_prefix, sub_index, super_prefix, super_index, state.rho.size(), here});
        const bool holds =
            prove(state, sub_prefix, own.next, sub_bound - 1, super_prefix, super_index, super_bound, here);
        state.sigma.pop_back();
        if (holds) return true;
        drop_after(state, mark);
    }
    if (other.entry.kind == shape_kind::binder && super_bound > 0) {
        const derivation_mark mark = mark_of(state);
        state.sigma.push_back(assumption{sub_prefix, sub_index, super_prefix, super_index, state.rho.size(), here});
        const bool holds =
            prove(state, sub_prefix, sub_index, sub_bound, super_prefix, other.next, super_bound - 1, here);
        state.sigma.pop_back();
        if (holds) return true;
        drop_after(state, mark);
    }
    return false;
}

// True when the graph holds a wrapper below the top, or a payload that
// a payload registration marks as no label or as not sendable.
// The graph lists its nodes in the order a depth-first walk reaches
// them, so the wrappers at the top come first and every node from `top`
// on is below them.
[[nodiscard]] consteval bool is_outside_the_check(const ::foundation::algebra::transition::graph_view& graph,
                                                  std::size_t top) {
    for (std::size_t index = top; index < graph.nodes.size(); ++index) {
        const ::foundation::algebra::transition::graph_node& node = graph.nodes[index];
        if (node.entry.kind == ::foundation::algebra::transition::shape_kind::wrapper) return true;
        if (node.has_restricted_payload) return true;
    }
    return false;
}

// Exit preservation over a derivation.  A configuration can end when a
// path of the derivation reaches a configuration that rule end closes.
// Each configuration whose node of the supertype can end, as a node of
// the graph of the supertype, must be able to end.  So the subtype never
// removes an exit that the supertype offers where the derivation stands.
// This is the condition of the synchronous relation (refines in
// foundation/algebra/Transition.h), read on the derivation in place of
// the product.  Complexity: O(C·E) at worst for C configurations and E
// edges, one pass per level of loop-back.
[[nodiscard]] consteval bool keeps_exits(const search& state) {
    const derivation& proof = state.proof;
    std::vector<std::uint8_t> can_end = proof.is_end;
    for (bool is_changed = true; is_changed;) {
        is_changed = false;
        for (std::size_t edge = proof.edge_from.size(); edge-- > 0;) {
            if (can_end[proof.edge_from[edge]] != 0 || can_end[proof.edge_to[edge]] == 0) continue;
            can_end[proof.edge_from[edge]] = 1;
            is_changed = true;
        }
    }
    for (std::size_t config = 0; config < proof.super_node.size(); ++config) {
        if (state.super.nodes[proof.super_node[config]].can_end && can_end[config] == 0) return false;
    }
    return true;
}

// The bounded check in one direction.  Top-level wrappers must agree,
// shape and value, and are then passed.  `ChecksExits` adds exit
// preservation.  The check of the dual direction runs without it: exit
// preservation asks the subtype to keep the exits of the supertype, and
// in the dual direction the roles of the two are swapped.
template <typename Sub, typename Super, std::size_t Capacity, bool ChecksExits>
consteval bool bounded() {
    subtype::require_registered_spine<Sub>();
    subtype::require_registered_spine<Super>();
    if (!is_well_formed_v<Sub> || !is_well_formed_v<Super>) return false;
    search state{};
    state.sub = ::foundation::algebra::transition::graph_of(protocol_registry, ^^Sub);
    state.super = ::foundation::algebra::transition::graph_of(protocol_registry, ^^Super);
    state.axioms = ^^::fixy::session::payload_axioms;
    state.capacity = Capacity;
    std::size_t sub_top = 0;
    std::size_t super_top = 0;
    while (state.sub.nodes[sub_top].entry.kind == ::foundation::algebra::transition::shape_kind::wrapper
           || state.super.nodes[super_top].entry.kind == ::foundation::algebra::transition::shape_kind::wrapper) {
        const ::foundation::algebra::transition::graph_node& own = state.sub.nodes[sub_top];
        const ::foundation::algebra::transition::graph_node& other = state.super.nodes[super_top];
        if (own.entry.shape != other.entry.shape || own.value != other.value) return false;
        sub_top = own.next;
        super_top = other.next;
    }
    if (is_outside_the_check(state.sub, sub_top) || is_outside_the_check(state.super, super_top)) {
        return false;
    }
    if (!prove(state, {}, sub_top, Capacity + 1, {}, super_top, Capacity + 1, ::foundation::algebra::transition::npos)) {
        return false;
    }
    return !ChecksExits || keeps_exits(state);
}

// One direction of the bounded check, as its own constant evaluation,
// so each direction has the full operation budget of the build.
template <typename Sub, typename Super, std::size_t Capacity, bool ChecksExits>
inline constexpr bool bounded_v = bounded<Sub, Super, Capacity, ChecksExits>();

// The synchronous relation first, then each direction of the bounded
// check only when the step before it did not decide.  Each call of a
// consteval function in a variable initializer is evaluated where it
// stands, also on the side of a || that is not needed, so the order is
// made by `if constexpr` and not by the operators.
template <typename Sub, typename Super, std::size_t Capacity>
consteval bool holds() {
    if constexpr (is_subtype_sync_v<Sub, Super>) {
        return true;
    } else if constexpr (!bounded_v<Sub, Super, Capacity, true>) {
        return false;
    } else {
        return bounded_v<dual_of_t<Super>, dual_of_t<Sub>, Capacity, false>;
    }
}

}  // namespace detail::async

// True when the channel type states a capacity of one message or more.
template <typename Channel>
concept StatesChannelCapacity = requires {
    { std::remove_cvref_t<Channel>::channel_capacity } -> std::convertible_to<std::size_t>;
} && (static_cast<std::size_t>(std::remove_cvref_t<Channel>::channel_capacity) > 0);

template <typename Channel>
    requires StatesChannelCapacity<Channel>
inline constexpr std::size_t channel_capacity_v =
    static_cast<std::size_t>(std::remove_cvref_t<Channel>::channel_capacity);

template <typename Sub, typename Super, typename Channel>
    requires StatesChannelCapacity<Channel>
inline constexpr bool is_subtype_async_v = detail::async::holds<Sub, Super, channel_capacity_v<Channel>>();

template <typename Sub, typename Super, typename Channel>
concept SubtypeAsync = StatesChannelCapacity<Channel> && is_subtype_async_v<Sub, Super, Channel>;

template <typename Sub, typename Super, typename Channel>
struct AsyncSubtypeQuery {};

template <typename Q>
struct is_async_subtype : std::false_type {};
template <typename Sub, typename Super, typename Channel>
    requires StatesChannelCapacity<Channel>
struct is_async_subtype<AsyncSubtypeQuery<Sub, Super, Channel>>
    : std::bool_constant<is_subtype_async_v<Sub, Super, Channel>> {};

template <typename Sub, typename Super, typename Channel>
    requires StatesChannelCapacity<Channel>
consteval void assert_subtype_async() noexcept {
    static_assert(is_subtype_async_v<Sub, Super, Channel>,
                  "fixy::session::diagnostic [Subtype_Async_Not_Proven]: assert_subtype_async<Sub, Super, "
                  "Channel>: the bounded check did not prove that Sub refines Super on a channel with the "
                  "capacity that Channel states.  The check refuses a pair it cannot prove.  Usual causes: "
                  "the subtype sends more messages ahead than the capacity holds, the subtype moves an input "
                  "ahead of an output, a message is left in a buffer at the end, the loops need more "
                  "unfolds than the capacity plus one, or the subtype removes an exit that the supertype "
                  "offers, so a run that could end cannot.");
}

}  // namespace fixy::session

// ── Armed cells ──────────────────────────────────────────────────────

namespace fixy::session::detail::subtype_armed_witness {
struct Ping {};
struct Stop {};
using Narrow = ::fixy::session::Select<::fixy::session::Send<Ping, ::fixy::session::End>>;
using Wide = ::fixy::session::Select<::fixy::session::Send<Ping, ::fixy::session::End>,
                                     ::fixy::session::Send<Stop, ::fixy::session::End>>;
using Early = ::fixy::session::Send<Ping, ::fixy::session::Recv<Stop, ::fixy::session::End>>;
using Late = ::fixy::session::Recv<Stop, ::fixy::session::Send<Ping, ::fixy::session::End>>;
using EarlyTwice = ::fixy::session::Send<Ping, Early>;
using LateTwice = ::fixy::session::Recv<Stop, ::fixy::session::Recv<Stop, ::fixy::session::Send<
                                                  Ping, ::fixy::session::Send<Ping, ::fixy::session::End>>>>;
// Channel ends that state a capacity.
struct OneSlot {
    static constexpr std::size_t channel_capacity = 1;
};
struct FourSlots {
    static constexpr std::size_t channel_capacity = 4;
};
}  // namespace fixy::session::detail::subtype_armed_witness

template <>
struct foundation::contracts::armed_cell<::fixy::session::is_sync_subtype> {
    using accepts = witnesses<
        ::fixy::session::SubtypeQuery<::fixy::session::End, ::fixy::session::End>,
        ::fixy::session::SubtypeQuery<::fixy::session::detail::subtype_armed_witness::Narrow,
                                      ::fixy::session::detail::subtype_armed_witness::Wide>>;
    using refuses = witnesses<
        int,
        ::fixy::session::SubtypeQuery<::fixy::session::detail::subtype_armed_witness::Wide,
                                      ::fixy::session::detail::subtype_armed_witness::Narrow>,
        ::fixy::session::SubtypeQuery<::fixy::session::detail::subtype_armed_witness::Early,
                                      ::fixy::session::detail::subtype_armed_witness::Late>>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::session::is_async_subtype> {
    using accepts = witnesses<
        ::fixy::session::AsyncSubtypeQuery<::fixy::session::detail::subtype_armed_witness::Early,
                                           ::fixy::session::detail::subtype_armed_witness::Late,
                                           ::fixy::session::detail::subtype_armed_witness::OneSlot>,
        ::fixy::session::AsyncSubtypeQuery<::fixy::session::End, ::fixy::session::End,
                                           ::fixy::session::detail::subtype_armed_witness::OneSlot>>;
    using refuses = witnesses<
        int,
        ::fixy::session::AsyncSubtypeQuery<::fixy::session::detail::subtype_armed_witness::Late,
                                           ::fixy::session::detail::subtype_armed_witness::Early,
                                           ::fixy::session::detail::subtype_armed_witness::FourSlots>,
        ::fixy::session::AsyncSubtypeQuery<::fixy::session::detail::subtype_armed_witness::EarlyTwice,
                                           ::fixy::session::detail::subtype_armed_witness::LateTwice,
                                           ::fixy::session::detail::subtype_armed_witness::OneSlot>>;
};
