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
// A label branch is matched by its position, because the handle sends
// the position as the label: select<I>() puts I on the wire, and the
// Offer of the peer dispatches on it.  A Select or an Offer with the
// same branches in another order is therefore another protocol, and the
// relation refuses it.  A branch that is no label, a crash branch for
// example, has no position on the wire.  It is matched by the payload
// it receives, wherever it stands (rule Sub-&, Barwell, Hou, Yoshida and
// Zhou, LMCS 2025, Definition 4.4).  So an Offer of the subtype can add a
// message branch before its crash branches.  The Sender note of an Offer
// is compared for equality, so each combinator is reflexive, the Offer
// with a note included.  An operand that is not well-formed is refused,
// and the reason says so.  An empty choice is not well-formed.
//
// One walk gives the verdict and its reason.  subtype_verdict_v names
// the first failed pair, in the order a depth-first walk reaches it,
// and subtype_reason_t turns the verdict into a type.  No second walk
// exists that could disagree with the first.
//
// The relation is closed under duality: when T refines U, the dual of U
// refines the dual of T.  The payload variance flips with the shape,
// the branch rule of Select mirrors the rule of Offer, and the vendor
// value is invariant.
//
// ── The asynchronous relation ─────────────────────────────────────────
//
// Precise asynchronous subtyping is undecidable, also for two parties
// (Bravetti, Carbone and Zavattaro, 2017; Lange and Yoshida, 2017).
// is_subtype_async_v<T, U, Capacity> is a sound check that can fail to
// prove a true pair.  A pair it cannot prove is refused.
//
// The check is the algorithm of Cutner, Yoshida and Vassor, Rumpsteak
// (PPoPP 2022, section 3.2, rules oi, oo, ii, io, sub, asm, μL and μR),
// for one peer.  With one peer, an output of the subtype can move ahead
// of inputs, and nothing else can move.  The transitivity rule is not
// used, which keeps the check sound and makes it less complete.
//
// Capacity is the capacity of the channel.  It bounds two things:
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
//   Tagged<T, V>               ⩽  T                     for a tag V in
//                                                        droppable_tags
//   NumericalTier<Tight, T>    ⩽  NumericalTier<Loose, T>
//
// The implication is the relation of fixy/Refined.h, closed here through
// its admitted edges: P implies Q when a chain of implications through
// the edge endpoints joins them.
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
#include <fixy/session/Protocol.h>
#include <foundation/algebra/Transition.h>
#include <foundation/algebra/lattices/ToleranceLattice.h>
#include <foundation/contracts/Armed.h>
#include <foundation/diag/FailClosed.h>

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
}  // namespace droppable_tags

namespace detail::payload_order {

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

template <typename A, typename B>
inline constexpr bool implies_step_v = ::fixy::refined::implies_types<A, B>();

[[nodiscard]] consteval bool implies_directly(std::meta::info premise, std::meta::info conclusion) {
    if (premise == conclusion) return true;
    return std::meta::extract<bool>(std::meta::substitute(^^implies_step_v, {premise, conclusion}));
}

// P implies Q through a chain whose inner nodes are endpoints of the
// admitted edges of fixy/Refined.h.  The edges are finite, so the walk
// stops.  Complexity: O(E²) implication queries for E edges.
[[nodiscard]] consteval bool implies_closed(std::meta::info premise, std::meta::info conclusion) {
    std::vector<std::meta::info> reached{premise};
    for (std::size_t cursor = 0; cursor < reached.size(); ++cursor) {
        const std::meta::info from = reached[cursor];
        if (implies_directly(from, conclusion)) return true;
        for (const std::meta::info member : std::meta::members_of(^^::fixy::refined::admitted_implications,
                                                                  std::meta::access_context::unchecked())) {
            if (!::foundation::fail_closed::is_edge(member)) continue;
            const ::foundation::fail_closed::edge_ends ends = ::foundation::fail_closed::ends_of(member);
            if (!implies_directly(from, ends.from)) continue;
            bool is_known = false;
            for (const std::meta::info seen : reached) is_known = is_known || seen == ends.to;
            if (!is_known) reached.push_back(ends.to);
        }
    }
    return false;
}

template <typename T>
inline constexpr bool refinement_drops_v = refinement_parts<T>::is_refinement && !refinement_parts<T>::is_sealed;

template <typename T>
using refinement_value_t = typename refinement_parts<T>::value_type;

template <typename T, typename U>
inline constexpr bool refinement_weakens_v = false;
template <auto P, auto Q, typename X, bool Sealed>
inline constexpr bool refinement_weakens_v<::fixy::Refinement<P, X, Sealed>, ::fixy::Refinement<Q, X, Sealed>> =
    implies_closed(std::meta::dealias(^^::fixy::refined::predicate_t<P>),
                   std::meta::dealias(^^::fixy::refined::predicate_t<Q>));

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

}  // namespace payload_axioms

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

template <typename ClientProto, typename ServerProto>
concept CompatibleClient = is_subtype_sync_v<ClientProto, dual_of_t<ServerProto>>;

template <typename ServerProto, typename ClientProto>
concept CompatibleServer = is_subtype_sync_v<ServerProto, dual_of_t<ClientProto>>;

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
    static_assert(CompatibleClient<ClientProto, ServerProto>,
                  detail::subtype::refusal_message<ClientProto, dual_of_t<ServerProto>>(
                      "assert_compatible_client: the client must refine the dual of the server"));
}

template <typename ServerProto, typename ClientProto>
consteval void assert_compatible_server() noexcept {
    static_assert(CompatibleServer<ServerProto, ClientProto>,
                  detail::subtype::refusal_message<ServerProto, dual_of_t<ClientProto>>(
                      "assert_compatible_server: the server must refine the dual of the client"));
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

struct action {
    bool is_output = false;
    bool is_label = false;
    std::size_t label = 0;
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
};

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
    return left.is_output == right.is_output && left.is_label == right.is_label && left.label == right.label
           && left.payload == right.payload && left.note == right.note;
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
// in the payload order for that direction.
[[nodiscard]] consteval bool matches(std::meta::info axioms, const action& sub, const action& super) {
    if (sub.is_output != super.is_output || sub.is_label != super.is_label) return false;
    if (sub.is_label) return sub.label == super.label && sub.note == super.note;
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
        result.push_back(move{action{is_output, false, 0, node.payload, {}}, node.next});
        return result;
    }
    for (std::size_t branch = 0; branch < node.child_count; ++branch) {
        result.push_back(move{action{is_output, true, branch, {}, node.annotation},
                              graph.children[node.first_child + branch]});
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
                     std::vector<action> super_prefix, std::size_t super_index, std::size_t super_bound);

consteval bool exchange(search& state, const std::vector<action>& sub_prefix, std::size_t sub_index,
                        std::size_t sub_bound, const std::vector<action>& super_prefix, std::size_t super_index,
                        std::size_t super_bound) {
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
        state.rho.push_back(mine.act);
        const bool holds = prove(state, next_sub, mine.next, sub_bound, next_super, theirs.next, super_bound);
        state.rho.pop_back();
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
                     std::vector<action> super_prefix, std::size_t super_index, std::size_t super_bound) {
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
    // Rule end.
    if (sub_prefix.empty() && super_prefix.empty() && own.entry.kind == shape_kind::terminal
        && other.entry.kind == shape_kind::terminal) {
        return own.entry.shape == other.entry.shape;
    }
    // Rule asm.
    for (const assumption& earlier : state.sigma) {
        if (earlier.sub_node == sub_index && earlier.super_node == super_index
            && same_prefix(earlier.sub_prefix, sub_prefix) && same_prefix(earlier.super_prefix, super_prefix)
            && covers(state.rho, earlier.rho_length, super_prefix)) {
            return true;
        }
    }
    // Rules oi, oo, ii and io.
    if (is_action_node(own) && is_action_node(other)) {
        return exchange(state, sub_prefix, sub_index, sub_bound, super_prefix, super_index, super_bound);
    }
    // Rules μL and μR.
    if (own.entry.kind == shape_kind::binder && sub_bound > 0) {
        state.sigma.push_back(assumption{sub_prefix, sub_index, super_prefix, super_index, state.rho.size()});
        const bool holds = prove(state, sub_prefix, own.next, sub_bound - 1, super_prefix, super_index, super_bound);
        state.sigma.pop_back();
        if (holds) return true;
    }
    if (other.entry.kind == shape_kind::binder && super_bound > 0) {
        state.sigma.push_back(assumption{sub_prefix, sub_index, super_prefix, super_index, state.rho.size()});
        const bool holds = prove(state, sub_prefix, sub_index, sub_bound, super_prefix, other.next, super_bound - 1);
        state.sigma.pop_back();
        if (holds) return true;
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

// The bounded check in one direction.  Top-level wrappers must agree,
// shape and value, and are then passed.
template <typename Sub, typename Super, std::size_t Capacity>
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
    return prove(state, {}, sub_top, Capacity + 1, {}, super_top, Capacity + 1);
}

// One direction of the bounded check, as its own constant evaluation,
// so each direction has the full operation budget of the build.
template <typename Sub, typename Super, std::size_t Capacity>
inline constexpr bool bounded_v = bounded<Sub, Super, Capacity>();

// The synchronous relation first, then each direction of the bounded
// check only when the step before it did not decide.  Each call of a
// consteval function in a variable initializer is evaluated where it
// stands, also on the side of a || that is not needed, so the order is
// made by `if constexpr` and not by the operators.
template <typename Sub, typename Super, std::size_t Capacity>
consteval bool holds() {
    if constexpr (is_subtype_sync_v<Sub, Super>) {
        return true;
    } else if constexpr (!bounded_v<Sub, Super, Capacity>) {
        return false;
    } else {
        return bounded_v<dual_of_t<Super>, dual_of_t<Sub>, Capacity>;
    }
}

}  // namespace detail::async

template <typename Sub, typename Super, std::size_t Capacity>
inline constexpr bool is_subtype_async_v = detail::async::holds<Sub, Super, Capacity>();

template <typename Sub, typename Super, std::size_t Capacity>
concept SubtypeAsync = is_subtype_async_v<Sub, Super, Capacity>;

template <typename Sub, typename Super, std::size_t Capacity>
struct AsyncSubtypeQuery {};

template <typename Q>
struct is_async_subtype : std::false_type {};
template <typename Sub, typename Super, std::size_t Capacity>
struct is_async_subtype<AsyncSubtypeQuery<Sub, Super, Capacity>>
    : std::bool_constant<is_subtype_async_v<Sub, Super, Capacity>> {};

template <typename Sub, typename Super, std::size_t Capacity>
consteval void assert_subtype_async() noexcept {
    static_assert(is_subtype_async_v<Sub, Super, Capacity>,
                  "fixy::session::diagnostic [Subtype_Async_Not_Proven]: assert_subtype_async<Sub, Super, "
                  "Capacity>: the bounded check did not prove that Sub refines Super on a channel of this "
                  "capacity.  The check refuses a pair it cannot prove.  Usual causes: the subtype sends "
                  "more messages ahead than the capacity holds, the subtype moves an input ahead of an "
                  "output, a message is left in a buffer at the end, or the loops need more unfolds than "
                  "the capacity plus one.");
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
                                           ::fixy::session::detail::subtype_armed_witness::Late, 1>,
        ::fixy::session::AsyncSubtypeQuery<::fixy::session::End, ::fixy::session::End, 0>>;
    using refuses = witnesses<
        int,
        ::fixy::session::AsyncSubtypeQuery<::fixy::session::detail::subtype_armed_witness::Late,
                                           ::fixy::session::detail::subtype_armed_witness::Early, 4>,
        ::fixy::session::AsyncSubtypeQuery<::fixy::session::detail::subtype_armed_witness::Early,
                                           ::fixy::session::detail::subtype_armed_witness::Late, 0>>;
};
