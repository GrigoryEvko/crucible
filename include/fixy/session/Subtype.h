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
consteval ::foundation::algebra::transition::verdict sync_verdict(std::meta::info sub, std::meta::info super);

}  // namespace detail::subtype

namespace detail::payload_order {

template <typename T, typename U>
inline constexpr bool delegation_weakens_v = false;
template <typename P, typename Q, typename Resource, typename Policy, typename PS>
inline constexpr bool
    delegation_weakens_v<DelegatedSession<P, Resource, Policy, PS>, DelegatedSession<Q, Resource, Policy, PS>> =
        ::fixy::session::detail::subtype::sync_verdict(^^Q, ^^P).holds;

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
    || (::fixy::PredicateInvocableOn<P, X> && ::fixy::PredicateInvocableOn<Q, X> && ::fixy::PredicateImplies<P, Q>);

template <typename T>
inline constexpr bool tagged_drops_v =
    tagged_parts<T>::is_tagged
    && ::foundation::fail_closed::admits(^^::fixy::session::droppable_tags, ^^typename tagged_parts<T>::tag, ^^void);

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

// True when each node of the spine of the protocol is registered and
// coherent.  The two concepts of fixy/session/Protocol.h keep the answer
// for each node type, so a node that two protocols share is read one
// time.
[[nodiscard]] consteval bool spine_is_sound(std::meta::info protocol) {
    const std::meta::info type = std::meta::dealias(protocol);
    return std::meta::extract<bool>(std::meta::substitute(^^::fixy::session::detail::SpineRegisteredBelow, {type}))
        && std::meta::extract<bool>(std::meta::substitute(^^::fixy::session::detail::SpineCoherentBelow, {type}));
}

// The answer of spine_is_sound for one protocol, as a concept.
template <typename P>
concept SoundSpine = spine_is_sound(^^P);

// The answer of a concept of one type argument for the protocol.  The
// compiler keeps the satisfaction of each concept-id, so the relation
// walks each protocol one time.  A call of a function over reflections
// that allocates is evaluated again at each call, and the relation asks
// about each protocol once for each pair that holds it.
[[nodiscard]] consteval bool holds_for(std::meta::info concept_template, std::meta::info protocol) {
    return std::meta::extract<bool>(std::meta::substitute(concept_template, {protocol}));
}

// Both spines must hold registered combinators only.  The relation
// reads every node, so a combinator it does not know stops the build.
// The refusal is a function at namespace scope that is not a template, so
// no translation unit can specialize it away.  It throws, so the constant
// evaluation that asked stops, and the diagnostic prints the message.
consteval void require_registered_spine(std::meta::info protocol) {
    if (holds_for(^^SoundSpine, protocol)) return;
    namespace tr = ::foundation::algebra::transition;
    const std::meta::info missing = tr::first_unregistered(protocol_registry, protocol);
    if (missing != std::meta::info{}) {
        throw std::meta::exception(
            ::fixy::session::detail::refusal_text(tr::unregistered_message(unregistered_prefix, missing)), missing);
    }
    const auto incoherent = tr::first_incoherent(protocol_registry, protocol);
    if (incoherent.reason != tr::incoherence::none) {
        throw std::meta::exception(
            ::fixy::session::detail::refusal_text(tr::incoherent_message(incoherent_prefix, incoherent)), protocol);
    }
}

// A function at namespace scope that is not a template, so no
// translation unit can specialize the verdict.
consteval ::foundation::algebra::transition::verdict sync_verdict(std::meta::info sub, std::meta::info super) {
    require_registered_spine(sub);
    require_registered_spine(super);
    if (!holds_for(^^::fixy::session::is_well_formed_v, sub)) {
        return {false, ::foundation::algebra::transition::mismatch::ill_formed, sub, {}};
    }
    if (!holds_for(^^::fixy::session::is_well_formed_v, super)) {
        return {false, ::foundation::algebra::transition::mismatch::ill_formed, {}, super};
    }
    return ::foundation::algebra::transition::refines(protocol_registry, ^^::fixy::session::payload_axioms, sub, super);
}

}  // namespace detail::subtype

// The gate reads the verdict through SubtypeSync.  The value spellings
// below read the same function, so they give the answer of the gate, and a
// specialization of one of them changes only what its author reads.
template <typename Sub, typename Super>
concept SubtypeSync = detail::subtype::sync_verdict(^^Sub, ^^Super).holds;

template <typename Sub, typename Super>
inline constexpr ::foundation::algebra::transition::verdict subtype_verdict_v =
    detail::subtype::sync_verdict(^^Sub, ^^Super);

template <typename Sub, typename Super>
inline constexpr ::foundation::algebra::transition::mismatch subtype_mismatch_v = subtype_verdict_v<Sub, Super>.reason;

template <typename Sub, typename Super>
inline constexpr bool is_subtype_sync_v = SubtypeSync<Sub, Super>;

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
    using type = std::conditional_t<
        found.holds, SubtypeOk,
        SubtypeRejection<found.reason, typename[:or_void(found.sub):], typename[:or_void(found.super):]>>;
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
concept EquivalentSync = SubtypeSync<Sub, Super> && SubtypeSync<Super, Sub>;

template <typename Sub, typename Super>
inline constexpr bool equivalent_sync_v = EquivalentSync<Sub, Super>;

template <typename Sub, typename Super>
concept StrictSubtypeSync = SubtypeSync<Sub, Super> && !SubtypeSync<Super, Sub>;

template <typename Sub, typename Super>
inline constexpr bool is_strict_subtype_sync_v = StrictSubtypeSync<Sub, Super>;

namespace detail::subtype {

template <typename... Ts>
consteval bool chain_holds() {
    if constexpr (sizeof...(Ts) < 2) {
        return true;
    } else {
        return []<typename First, typename Second, typename... Rest>(
                   std::type_identity<First>, std::type_identity<Second>, std::type_identity<Rest>...) {
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
concept CompatibleClient =
    SubtypeSync<ClientProto, dual_of_t<ServerProto>> && SubtypeSync<ServerProto, dual_of_t<ClientProto>>;

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
//
// The search below is the recursion of the rules.  Its data has a form
// that needs few operations in a constant evaluation.  The form changes
// only the cost: each step spends the same fuel, goes to the same
// configurations in the same order and records the same derivation as a
// search that copies vectors of actions.  These are the reasons:
//
//   An action is a code.  Two actions have one code when they are equal
//     field by field, and only then.  Two prefixes are equal when their
//     codes are equal, and only then.
//   A prefix is a run of codes in one pool, with its count of outputs.
//     fits and covers read the counts, and the counts are the counts of
//     the actions.
//   The moves of a node are the moves that a step reads at that node, in
//     the same order.
//   The search keeps the answer of the payload order for each pair of
//     payloads that it asks, because the answer is a function of the two
//     payloads.
//   rho keeps the count of outputs at each length of ρ.  covers reads
//     the directions of ρ' from two counts.
//   A failed attempt sets the tops of the derivation to their values
//     before the attempt, and this drops what the attempt recorded.
//
// The state of a step is its prefixes, its two nodes, its two unfold
// bounds, the length of ρ, the assumptions and the fuel that is left.
// Because of the assumptions and the directions of ρ, almost no state
// occurs two times in one search.  A table of answers for each state
// cannot decrease the work.  The cost of a step is the cost of its
// operations, and a change of a vector costs the most of them.

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

// The code of an action in one search.  The search keeps a table that
// holds each action that its steps read one time.  A code is the place of
// the action in that table, shifted left by two bits.  Bit 1 is set for a
// label action, and bit 0 is set for an output.  Two actions get one
// code when same_action holds for them, and only then.
using action_code = std::uint32_t;

inline constexpr action_code output_bit = 1U;
inline constexpr action_code label_bit = 2U;

struct move {
    action_code act = 0;
    std::size_t next = ::foundation::algebra::transition::npos;
};

// A prefix: `length` codes from place `start` of the pool of the search,
// of which `outputs` are outputs.  A prefix that a call of prove receives
// does not change while that call runs, because each attempt writes its
// new prefixes past the prefixes of the calls on the current path.
struct prefix {
    std::size_t start = 0;
    std::size_t length = 0;
    std::size_t outputs = 0;
};

struct assumption {
    prefix sub_prefix{};
    std::size_t sub_node = ::foundation::algebra::transition::npos;
    prefix super_prefix{};
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
struct configuration {
    std::size_t super_node = ::foundation::algebra::transition::npos;
    bool is_end = false;
};

struct edge {
    std::size_t from = ::foundation::algebra::transition::npos;
    std::size_t to = ::foundation::algebra::transition::npos;
};

// The answer of the payload order for the codes of two steps.
struct payload_answer {
    action_code sub = 0;
    action_code super = 0;
    bool holds = false;
};

// A step of the search changes no vector: each run of the search is a
// stack of foundation/algebra/Transition.h.
using ::foundation::algebra::transition::make_room;
using ::foundation::algebra::transition::push;
using ::foundation::algebra::transition::stack;

// The fuel of one direction of the search.  A search that has no fuel
// left has not proven the pair, and the pair is refused.  Each step
// spends units by a fixed formula.  A call of prove spends one unit, one
// unit for each code of its two prefixes and one unit for each action of
// ρ.  For each assumption, it spends one more unit and one unit for each
// code of its two prefixes.  An attempt spends two units and one unit
// for each code of the two prefixes that it extends.  The amount and the
// formula decide which pairs the check proves.  A change of one of them
// can change an answer.
//
// The amount keeps the search inside the constexpr operation limit of
// the build (-fconstexpr-ops-limit, the row constexpr-ops of
// utils/scripts/budgets.txt, 33,554,432 operations).  A hard pair is
// refused with an answer and never stops the build.  The evaluation of
// the differential corpus that uses the most operations is a search that
// spends all its fuel, in test/session_oracle/generated_fixy_subtype_00.cpp.
// It uses approximately 2.2 million operations: approximately 34
// operations for each unit, and 7% of the limit.  No pair of the corpus
// fails with this amount and holds with two times this amount.
inline constexpr std::size_t search_fuel = std::size_t{1} << 16;

// The state of one direction of the search.
//
//   node_moves  two places for each node of the subtype graph, then two
//               for each node of the supertype graph: the place of the
//               first move of the node in `moves`, and the count of its
//               moves.  The first place holds npos until a step reads
//               the moves of the node.
//   actions     the table of the actions, at the places that the codes
//               name.
//   pool        the codes of the prefixes.
//   rho         at place k, the count of outputs among the first k + 1
//               actions of ρ.  `top` is the length of ρ.
//   sigma       the assumptions.
//   configurations, edges
//               the derivation.
struct search {
    ::foundation::algebra::transition::graph_view sub{};
    ::foundation::algebra::transition::graph_view super{};
    std::meta::info axioms{};
    std::size_t capacity = 0;
    std::size_t fuel = search_fuel;
    stack<std::size_t> node_moves{};
    std::size_t* node_items = nullptr;
    stack<action> actions{};
    stack<move> moves{};
    stack<payload_answer> payload_answers{};
    stack<action_code> pool{};
    stack<std::size_t> rho{};
    stack<assumption> sigma{};
    stack<configuration> configurations{};
    stack<edge> edges{};
};

// Records the edge from one configuration to another.  A configuration
// with no parent is the root, and no edge leads to it.
consteval void add_edge(search& state, std::size_t from, std::size_t to) {
    if (from == ::foundation::algebra::transition::npos) return;
    push(state.edges, edge{from, to});
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

// True when the two prefixes hold the same codes in the same order.
[[nodiscard]] consteval bool same_codes(const search& state, prefix left, prefix right) {
    if (left.length != right.length) return false;
    for (std::size_t index = 0; index < left.length; ++index) {
        if (state.pool.items[left.start + index] != state.pool.items[right.start + index]) return false;
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

// The answer of matches for the actions of two codes, the subtype action
// first.  The codes decide the answer in three cases:
//
//   Equal codes name one action.  matches holds for an action and the
//     same action, because the payload order is reflexive.
//   Codes of two directions do not match.
//   A label action matches only an action with all its fields equal.
//     Two different codes of which one is a label do not match.
//
// For two different steps of one direction, matches asks the payload
// order.  The search keeps the answer for the pair of codes, because the
// answer is a function of the two payloads.
[[nodiscard]] consteval bool matches_code(search& state, action_code sub, action_code super) {
    if (sub == super) return true;
    if (((sub ^ super) & output_bit) != 0 || ((sub | super) & label_bit) != 0) return false;
    for (std::size_t index = 0; index < state.payload_answers.top; ++index) {
        const payload_answer& known = state.payload_answers.items[index];
        if (known.sub == sub && known.super == super) return known.holds;
    }
    const bool holds = matches(state.axioms, state.actions.items[sub >> 2U], state.actions.items[super >> 2U]);
    push(state.payload_answers, payload_answer{sub, super, holds});
    return holds;
}

// Prefix reduction for one peer.  An input at the head of the subtype
// prefix matches an input at the head of the supertype prefix (rule →i).
// An output at the head of the subtype prefix matches the first output
// of the supertype prefix after its inputs (rules →o and →B).  Nothing
// else moves.  The loop stops at a head that cannot move, and a second
// reduction of a reduced pair changes nothing.  The supertype prefix
// loses one code in place.  It must be a prefix that the caller wrote
// for this reduction.
consteval void reduce(search& state, prefix& sub_prefix, prefix& super_prefix) {
    while (sub_prefix.length > 0) {
        const action_code head = state.pool.items[sub_prefix.start];
        std::size_t partner = ::foundation::algebra::transition::npos;
        if ((head & output_bit) != 0) {
            for (std::size_t index = 0; index < super_prefix.length; ++index) {
                if ((state.pool.items[super_prefix.start + index] & output_bit) != 0) {
                    partner = index;
                    break;
                }
            }
        } else if (super_prefix.length > 0 && (state.pool.items[super_prefix.start] & output_bit) == 0) {
            partner = 0;
        }
        if (partner == ::foundation::algebra::transition::npos
            || !matches_code(state, head, state.pool.items[super_prefix.start + partner])) {
            return;
        }
        const action_code removed = state.pool.items[super_prefix.start + partner];
        ++sub_prefix.start;
        --sub_prefix.length;
        sub_prefix.outputs -= head & output_bit;
        super_prefix.outputs -= removed & output_bit;
        if (partner == 0) {
            ++super_prefix.start;
        } else {
            for (std::size_t index = partner; index + 1 < super_prefix.length; ++index) {
                state.pool.items[super_prefix.start + index] = state.pool.items[super_prefix.start + index + 1];
            }
        }
        --super_prefix.length;
    }
}

// The subtype prefix holds the outputs the subtype sent ahead, and the
// supertype prefix holds the inputs the peer sent before the subtype
// received them.  Each is a count of messages in one buffer.
[[nodiscard]] consteval bool fits(const search& state, prefix sub_prefix, prefix super_prefix) {
    return sub_prefix.outputs <= state.capacity && super_prefix.length - super_prefix.outputs <= state.capacity;
}

// The count of outputs among the first `length` actions of ρ.
[[nodiscard]] consteval std::size_t outputs_before(const search& state, std::size_t length) {
    return length == 0 ? 0 : state.rho.items[length - 1];
}

// act(ρ') ⊇ act(π'): since the assumption, the subtype did an action of
// each direction that the supertype prefix still holds.  ρ' is ρ from
// place `from`, and each action of ρ' that is not an output is an input.
[[nodiscard]] consteval bool covers(const search& state, std::size_t from, prefix super_prefix) {
    const bool needs_output = super_prefix.outputs > 0;
    const bool needs_input = super_prefix.length > super_prefix.outputs;
    const std::size_t outputs = outputs_before(state, state.rho.top) - outputs_before(state, from);
    const bool has_output = outputs > 0;
    const bool has_input = state.rho.top - from > outputs;
    return (!needs_output || has_output) && (!needs_input || has_input);
}

[[nodiscard]] consteval bool is_action_node(const ::foundation::algebra::transition::graph_node& node) {
    return node.entry.kind == ::foundation::algebra::transition::shape_kind::step
        || node.entry.kind == ::foundation::algebra::transition::shape_kind::choice;
}

// The code of an action.  The table gets the action when it does not
// hold it.  Complexity: linear in the actions of the table.
[[nodiscard]] consteval action_code code_of(search& state, const action& act) {
    std::size_t place = 0;
    while (place < state.actions.top && !same_action(state.actions.items[place], act))
        ++place;
    if (place == state.actions.top) push(state.actions, act);
    return static_cast<action_code>((place << 2U) | (act.is_label ? label_bit : 0U)
                                    | (act.is_output ? output_bit : 0U));
}

// The place in node_moves of the moves of node `index` of a graph.
// `offset` is zero for the subtype graph, and the node count of the
// subtype graph for the supertype graph.  The first read of a node
// writes its moves.  A step has one move: its payload, to its
// continuation.  A choice has one move for each branch, in the order of
// the branches, with the word that the handle sends for the branch.
[[nodiscard]] consteval std::size_t moves_of(search& state, const ::foundation::algebra::transition::graph_view& graph,
                                             std::size_t offset, std::size_t index) {
    const std::size_t slot = 2 * (offset + index);
    if (state.node_items[slot] != ::foundation::algebra::transition::npos) return slot;
    const ::foundation::algebra::transition::graph_node& node = graph.nodes[index];
    const bool is_output = node.entry.direction == ::foundation::algebra::transition::polarity::output;
    const std::size_t first = state.moves.top;
    if (node.entry.kind == ::foundation::algebra::transition::shape_kind::step) {
        const action_code code = code_of(state, action{is_output, false, false, 0, {}, node.payload, {}});
        push(state.moves, move{code, node.next});
    } else {
        for (std::size_t branch = 0; branch < node.child_count; ++branch) {
            const std::size_t child = graph.children[node.first_child + branch];
            const ::foundation::algebra::transition::graph_node& head = graph.nodes[child];
            const std::uint64_t label = node.is_keyed ? head.label_word : static_cast<std::uint64_t>(branch);
            const std::meta::info key = node.is_keyed ? head.label_key : std::meta::info{};
            const action_code code =
                code_of(state, action{is_output, true, node.is_keyed, label, key, {}, node.annotation});
            push(state.moves, move{code, child});
        }
    }
    state.node_items[slot] = first;
    state.node_items[slot + 1] = state.moves.top - first;
    return slot;
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

// The prefixes of each call are reduced.  The root call has two empty
// prefixes, attempt reduces the prefixes that it passes, and rules μL and
// μR pass the prefixes of their own call.  prove does not reduce them
// again.
consteval bool prove(search& state, prefix sub_prefix, std::size_t sub_index, std::size_t sub_bound,
                     prefix super_prefix, std::size_t super_index, std::size_t super_bound, std::size_t parent);

// One move of the subtype against one move of the supertype.  The new
// prefixes are the old prefixes with the two actions added, reduced.
// attempt writes them above the top of the pool and lowers the top again
// when it returns.  The pool then holds only the prefixes of the calls
// on the current path.  A failed attempt drops what it recorded in the
// derivation.  The moves are values, because a later step can add moves
// to the table and make a reference into it not valid.
consteval bool attempt(search& state, prefix sub_prefix, std::size_t sub_bound, prefix super_prefix,
                       std::size_t super_bound, move mine, move theirs, std::size_t here) {
    if (!spend(state, 2 + sub_prefix.length + super_prefix.length)) return false;
    const std::size_t base = state.pool.top;
    make_room(state.pool, sub_prefix.length + super_prefix.length + 2);
    action_code* const codes = state.pool.items;
    prefix next_sub{base, sub_prefix.length + 1, sub_prefix.outputs + (mine.act & output_bit)};
    for (std::size_t index = 0; index < sub_prefix.length; ++index)
        codes[next_sub.start + index] = codes[sub_prefix.start + index];
    codes[next_sub.start + sub_prefix.length] = mine.act;
    prefix next_super{next_sub.start + next_sub.length, super_prefix.length + 1,
                      super_prefix.outputs + (theirs.act & output_bit)};
    for (std::size_t index = 0; index < super_prefix.length; ++index)
        codes[next_super.start + index] = codes[super_prefix.start + index];
    codes[next_super.start + super_prefix.length] = theirs.act;
    state.pool.top = next_super.start + next_super.length;
    reduce(state, next_sub, next_super);
    bool holds = false;
    if (fits(state, next_sub, next_super)) {
        const std::size_t configurations = state.configurations.top;
        const std::size_t edges = state.edges.top;
        push(state.rho, outputs_before(state, state.rho.top) + (mine.act & output_bit));
        holds = prove(state, next_sub, mine.next, sub_bound, next_super, theirs.next, super_bound, here);
        --state.rho.top;
        if (!holds) {
            state.configurations.top = configurations;
            state.edges.top = edges;
        }
    }
    state.pool.top = base;
    return holds;
}

consteval bool exchange(search& state, prefix sub_prefix, std::size_t sub_index, std::size_t sub_bound,
                        prefix super_prefix, std::size_t super_index, std::size_t super_bound, std::size_t here) {
    const std::size_t own_slot = moves_of(state, state.sub, 0, sub_index);
    const std::size_t other_slot = moves_of(state, state.super, state.sub.nodes.size(), super_index);
    const std::size_t own_first = state.node_items[own_slot];
    const std::size_t own_end = own_first + state.node_items[own_slot + 1];
    const std::size_t other_first = state.node_items[other_slot];
    const std::size_t other_end = other_first + state.node_items[other_slot + 1];
    const bool sub_sends =
        state.sub.nodes[sub_index].entry.direction == ::foundation::algebra::transition::polarity::output;
    const bool super_sends =
        state.super.nodes[super_index].entry.direction == ::foundation::algebra::transition::polarity::output;
    if (sub_sends && !super_sends) {
        // Rule oi: every output of the subtype against every input of
        // the supertype.
        for (std::size_t mine = own_first; mine < own_end; ++mine) {
            for (std::size_t theirs = other_first; theirs < other_end; ++theirs) {
                if (!attempt(state, sub_prefix, sub_bound, super_prefix, super_bound, state.moves.items[mine],
                             state.moves.items[theirs], here)) {
                    return false;
                }
            }
        }
        return true;
    }
    if (sub_sends && super_sends) {
        // Rule oo: each output of the subtype against some output of the
        // supertype.
        for (std::size_t mine = own_first; mine < own_end; ++mine) {
            bool found = false;
            for (std::size_t theirs = other_first; theirs < other_end && !found; ++theirs) {
                found = attempt(state, sub_prefix, sub_bound, super_prefix, super_bound, state.moves.items[mine],
                                state.moves.items[theirs], here);
            }
            if (!found) return false;
        }
        return true;
    }
    if (!sub_sends && !super_sends) {
        // Rule ii: each input of the supertype against some input of the
        // subtype.
        for (std::size_t theirs = other_first; theirs < other_end; ++theirs) {
            bool found = false;
            for (std::size_t mine = own_first; mine < own_end && !found; ++mine) {
                found = attempt(state, sub_prefix, sub_bound, super_prefix, super_bound, state.moves.items[mine],
                                state.moves.items[theirs], here);
            }
            if (!found) return false;
        }
        return true;
    }
    // Rule io: some input of the subtype against some output of the
    // supertype.
    for (std::size_t mine = own_first; mine < own_end; ++mine) {
        for (std::size_t theirs = other_first; theirs < other_end; ++theirs) {
            if (attempt(state, sub_prefix, sub_bound, super_prefix, super_bound, state.moves.items[mine],
                        state.moves.items[theirs], here)) {
                return true;
            }
        }
    }
    return false;
}

consteval bool prove(search& state, prefix sub_prefix, std::size_t sub_index, std::size_t sub_bound,
                     prefix super_prefix, std::size_t super_index, std::size_t super_bound, std::size_t parent) {
    using ::foundation::algebra::transition::shape_kind;
    const std::size_t prefix_length = sub_prefix.length + super_prefix.length;
    const std::size_t rho_length = state.rho.top;
    if (!spend(state, 1 + prefix_length + rho_length + state.sigma.top * (1 + prefix_length))) return false;
    sub_index = to_binder(state.sub, sub_index);
    super_index = to_binder(state.super, super_index);
    if (sub_index == ::foundation::algebra::transition::npos
        || super_index == ::foundation::algebra::transition::npos) {
        return false;
    }
    const ::foundation::algebra::transition::graph_node& own = state.sub.nodes[sub_index];
    const ::foundation::algebra::transition::graph_node& other = state.super.nodes[super_index];
    if (own.entry.kind == shape_kind::wrapper || other.entry.kind == shape_kind::wrapper) return false;
    // This call is a configuration of the derivation.  A caller whose
    // attempt fails drops it again.
    const std::size_t here = state.configurations.top;
    push(state.configurations, configuration{super_index, false});
    add_edge(state, parent, here);
    // Rule end.
    if (prefix_length == 0 && own.entry.kind == shape_kind::terminal && other.entry.kind == shape_kind::terminal) {
        if (own.entry.shape != other.entry.shape) return false;
        state.configurations.items[here].is_end = true;
        return true;
    }
    // Rule asm.  The loop adds to no stack but the edges.  The reference
    // into sigma stays valid.
    for (std::size_t index = 0; index < state.sigma.top; ++index) {
        const assumption& earlier = state.sigma.items[index];
        if (earlier.sub_node == sub_index && earlier.super_node == super_index
            && same_codes(state, earlier.sub_prefix, sub_prefix)
            && same_codes(state, earlier.super_prefix, super_prefix)
            && covers(state, earlier.rho_length, super_prefix)) {
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
        const std::size_t configurations = state.configurations.top;
        const std::size_t edges = state.edges.top;
        push(state.sigma, assumption{sub_prefix, sub_index, super_prefix, super_index, rho_length, here});
        const bool holds =
            prove(state, sub_prefix, own.next, sub_bound - 1, super_prefix, super_index, super_bound, here);
        --state.sigma.top;
        if (holds) return true;
        state.configurations.top = configurations;
        state.edges.top = edges;
    }
    if (other.entry.kind == shape_kind::binder && super_bound > 0) {
        const std::size_t configurations = state.configurations.top;
        const std::size_t edges = state.edges.top;
        push(state.sigma, assumption{sub_prefix, sub_index, super_prefix, super_index, rho_length, here});
        const bool holds =
            prove(state, sub_prefix, sub_index, sub_bound, super_prefix, other.next, super_bound - 1, here);
        --state.sigma.top;
        if (holds) return true;
        state.configurations.top = configurations;
        state.edges.top = edges;
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
    stack<std::uint8_t> reaches_end{};
    make_room(reaches_end, state.configurations.top);
    std::uint8_t* const can_end = reaches_end.items;
    for (std::size_t config = 0; config < state.configurations.top; ++config)
        can_end[config] = state.configurations.items[config].is_end ? 1 : 0;
    for (bool is_changed = true; is_changed;) {
        is_changed = false;
        for (std::size_t index = state.edges.top; index-- > 0;) {
            const edge& link = state.edges.items[index];
            if (can_end[link.from] != 0 || can_end[link.to] == 0) continue;
            can_end[link.from] = 1;
            is_changed = true;
        }
    }
    for (std::size_t config = 0; config < state.configurations.top; ++config) {
        if (state.super.nodes[state.configurations.items[config].super_node].can_end && can_end[config] == 0) {
            return false;
        }
    }
    return true;
}

// The bounded check in one direction.  Top-level wrappers must agree,
// shape and value, and are then passed.  checks_exits adds exit
// preservation.  The check of the dual direction runs without it: exit
// preservation asks the subtype to keep the exits of the supertype, and
// in the dual direction the roles of the two are swapped.  A function at
// namespace scope that is not a template, so no translation unit can
// specialize the answer.
[[nodiscard]] consteval bool bounded(std::meta::info sub, std::meta::info super, std::size_t capacity,
                                     bool checks_exits) {
    subtype::require_registered_spine(sub);
    subtype::require_registered_spine(super);
    if (!subtype::holds_for(^^::fixy::session::is_well_formed_v, sub)
        || !subtype::holds_for(^^::fixy::session::is_well_formed_v, super)) {
        return false;
    }
    search state{};
    state.sub = ::foundation::algebra::transition::graph_of(protocol_registry, sub);
    state.super = ::foundation::algebra::transition::graph_of(protocol_registry, super);
    state.axioms = ^^::fixy::session::payload_axioms;
    state.capacity = capacity;
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
    const std::size_t slot_count = 2 * (state.sub.nodes.size() + state.super.nodes.size());
    make_room(state.node_moves, slot_count);
    state.node_items = state.node_moves.items;
    for (std::size_t slot = 0; slot < slot_count; ++slot)
        state.node_items[slot] = ::foundation::algebra::transition::npos;
    if (!prove(state, prefix{}, sub_top, capacity + 1, prefix{}, super_top, capacity + 1,
               ::foundation::algebra::transition::npos)) {
        return false;
    }
    return !checks_exits || keeps_exits(state);
}

}  // namespace detail::async

// True when the channel type states a capacity of one message or more.
template <typename Channel>
concept StatesChannelCapacity = requires {
    { std::remove_cvref_t<Channel>::channel_capacity } -> std::convertible_to<std::size_t>;
} && (static_cast<std::size_t>(std::remove_cvref_t<Channel>::channel_capacity) > 0);

// True when the two channel types state one capacity.
template <typename ChannelA, typename ChannelB>
concept StatesOneChannelCapacity = StatesChannelCapacity<ChannelA> && StatesChannelCapacity<ChannelB>
                                && (static_cast<std::size_t>(std::remove_cvref_t<ChannelA>::channel_capacity)
                                    == static_cast<std::size_t>(std::remove_cvref_t<ChannelB>::channel_capacity));

// True when Sub refines Super on a channel with the capacity that Channel
// states: the synchronous relation holds, or the bounded check proves
// each direction.  Each operand is its own constant evaluation, so each
// direction has the full operation budget of the build, and an operand is
// asked only when the operands before it leave the answer open.  No
// template decides an operand, so no translation unit can specialize the
// answer.
template <typename Sub, typename Super, typename Channel>
concept SubtypeAsync =
    StatesChannelCapacity<Channel>
    && (SubtypeSync<Sub, Super>
        || (detail::async::bounded(^^Sub, ^^Super,
                                   static_cast<std::size_t>(std::remove_cvref_t<Channel>::channel_capacity), true)
            && detail::async::bounded(detail::dual_type_of(^^Super), detail::dual_type_of(^^Sub),
                                      static_cast<std::size_t>(std::remove_cvref_t<Channel>::channel_capacity),
                                      false)));

// The value spellings read the gate, so they give its answer, and a
// specialization of one of them changes only what its author reads.
template <typename Channel>
    requires StatesChannelCapacity<Channel>
inline constexpr std::size_t channel_capacity_v =
    static_cast<std::size_t>(std::remove_cvref_t<Channel>::channel_capacity);

template <typename Sub, typename Super, typename Channel>
    requires StatesChannelCapacity<Channel>
inline constexpr bool is_subtype_async_v = SubtypeAsync<Sub, Super, Channel>;

template <typename Sub, typename Super, typename Channel>
struct AsyncSubtypeQuery {};

template <typename Q>
struct is_async_subtype : std::false_type {};
template <typename Sub, typename Super, typename Channel>
    requires StatesChannelCapacity<Channel>
struct is_async_subtype<AsyncSubtypeQuery<Sub, Super, Channel>>
    : std::bool_constant<SubtypeAsync<Sub, Super, Channel>> {};

template <typename Sub, typename Super, typename Channel>
    requires StatesChannelCapacity<Channel>
consteval void assert_subtype_async() noexcept {
    static_assert(SubtypeAsync<Sub, Super, Channel>,
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
using LateTwice = ::fixy::session::Recv<
    Stop, ::fixy::session::Recv<Stop, ::fixy::session::Send<Ping, ::fixy::session::Send<Ping, ::fixy::session::End>>>>;
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
    using accepts = witnesses<::fixy::session::SubtypeQuery<::fixy::session::End, ::fixy::session::End>,
                              ::fixy::session::SubtypeQuery<::fixy::session::detail::subtype_armed_witness::Narrow,
                                                            ::fixy::session::detail::subtype_armed_witness::Wide>>;
    using refuses = witnesses<int,
                              ::fixy::session::SubtypeQuery<::fixy::session::detail::subtype_armed_witness::Wide,
                                                            ::fixy::session::detail::subtype_armed_witness::Narrow>,
                              ::fixy::session::SubtypeQuery<::fixy::session::detail::subtype_armed_witness::Early,
                                                            ::fixy::session::detail::subtype_armed_witness::Late>>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::session::is_async_subtype> {
    using accepts =
        witnesses<::fixy::session::AsyncSubtypeQuery<::fixy::session::detail::subtype_armed_witness::Early,
                                                     ::fixy::session::detail::subtype_armed_witness::Late,
                                                     ::fixy::session::detail::subtype_armed_witness::OneSlot>,
                  ::fixy::session::AsyncSubtypeQuery<::fixy::session::End, ::fixy::session::End,
                                                     ::fixy::session::detail::subtype_armed_witness::OneSlot>>;
    using refuses =
        witnesses<int,
                  ::fixy::session::AsyncSubtypeQuery<::fixy::session::detail::subtype_armed_witness::Late,
                                                     ::fixy::session::detail::subtype_armed_witness::Early,
                                                     ::fixy::session::detail::subtype_armed_witness::FourSlots>,
                  ::fixy::session::AsyncSubtypeQuery<::fixy::session::detail::subtype_armed_witness::EarlyTwice,
                                                     ::fixy::session::detail::subtype_armed_witness::LateTwice,
                                                     ::fixy::session::detail::subtype_armed_witness::OneSlot>>;
};
