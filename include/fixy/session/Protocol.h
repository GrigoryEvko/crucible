#pragma once

// The session-type protocol DSL.  A protocol is a type built from Send,
// Recv, Select, Offer, Loop, Continue, End and VendorPinned.  The two
// endpoints of one channel agree when their protocols are duals.
//
// Select is an internal choice: this endpoint picks the branch and tells
// the peer which one.  Offer is an external choice: the peer picks, and
// this endpoint dispatches on the label it receives.  Duality swaps the
// two, so the Select of one side always faces the Offer of the other
// side, and a Send always faces a Recv.
//
// Recursion is iso-recursive.  A handle is never positioned at a Loop.
// The factory unrolls one iteration and positions the handle at the
// loop body, with the Loop itself as the LoopCtx.  Continue resolves
// against that context, so it binds the nearest Loop above it, and a
// nested Loop shadows the outer one for the length of its body.
//
// ── One algebra, one registry ─────────────────────────────────────────
//
// The combinators are registered in `fixy::session::combinators`, and
// every operation here is one algebra of the transition fold in
// foundation/algebra/Transition.h: duality, composition, composition at
// one branch, well-formedness, terminality and the empty-choice test.
// The registration contract is stated in that header.
//
// Each primary template reads the registry and fails closed.  A type
// that is not a registered combinator stops the build with a message
// that names it, so a combinator that a different header adds without
// a registration is refused, never passed.
//
// Each trait is also the entry point the fold uses for a child node.
// An explicit specialization of a trait for one node therefore answers
// at every depth, and the registry answers everywhere else.
//
// This header holds the type level only.  Nothing here has a runtime
// representation, so nothing here depends on the abandonment policy.
// The handle and that policy are in fixy/session/Handle.h.
//
// The EpochCtx context wrapper of the ported source is not here.  It
// reads EpochLattice and GenerationLattice, which foundation does not
// carry yet.  The LoopCtx traits keep their indirection, so EpochCtx is
// one specialization of each when those lattices land.

#include <foundation/algebra/Transition.h>
#include <foundation/algebra/lattices/VendorLattice.h>

#include <cstddef>
#include <meta>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace fixy::session {

using ::foundation::algebra::lattices::VendorBackend;

// ── Combinators ──────────────────────────────────────────────────────

template <typename T, typename Rest>
struct Send {
    using message_type = T;
    using next = Rest;
};

template <typename T, typename Rest>
struct Recv {
    using message_type = T;
    using next = Rest;
};

// Internal choice: this endpoint picks one of Branches...
template <typename... Branches>
struct Select {
    static constexpr std::size_t branch_count = sizeof...(Branches);
    using branches_tuple = std::tuple<Branches...>;
};

// Names the role that signals a choice, as the first template argument
// of an Offer or a Select:
//
//   Offer<Sender<Alice>, Recv<Msg, End>, Recv<Crash<Alice>, Recovery>>
//
// A two-party Offer needs no note, because "the peer" is clear.  A
// multiparty local protocol can hold Offers that different roles
// signal, and crash analysis must know which role signals an Offer.
// On a Select the note names the local role, which picks the branch.
// Duality keeps the note, so the two endpoints name the same role.  The
// note is not a branch.
template <typename Role>
struct Sender {
    using role_type = Role;
};

// The sender of an Offer with no note.  It is not a role a user writes.
// It tells a note from its absence at the type level, and it matches
// every peer in crash analysis.
struct AnonymousPeer {};

// External choice: the peer picks one of Branches...
template <typename... Branches>
struct Offer {
    static constexpr std::size_t branch_count = sizeof...(Branches);
    using branches_tuple = std::tuple<Branches...>;
    using sender = AnonymousPeer;
};

// `branch_count` and `branches_tuple` count the real branches only.
template <typename Role, typename... Branches>
struct Offer<Sender<Role>, Branches...> {
    static constexpr std::size_t branch_count = sizeof...(Branches);
    using branches_tuple = std::tuple<Branches...>;
    using sender = Role;
};

// A Select with a note.  `branch_count` and `branches_tuple` count the
// real branches only.
template <typename Role, typename... Branches>
struct Select<Sender<Role>, Branches...> {
    static constexpr std::size_t branch_count = sizeof...(Branches);
    using branches_tuple = std::tuple<Branches...>;
};

template <typename OfferType>
struct offer_sender {
    using type = typename OfferType::sender;
};

template <typename OfferType>
using offer_sender_t = typename offer_sender<OfferType>::type;

template <typename Body>
struct Loop {
    using body = Body;
};

struct Continue {};

struct End {};

// Declares the vendor that an upper-layer session runs against.  The
// structural operations see through it.  Refinement compares its value
// for equality, and well-formedness refuses VendorBackend::None, which
// names no kernel.
template <VendorBackend V, typename Proto>
struct VendorPinned : Proto {
    using protocol = Proto;
    static constexpr VendorBackend vendor_backend = V;
};

// The payloads that the registry below has a rule for.  Each is defined
// in the header of its layer: Crash in fixy/session/Crash.h and PeerMsg
// in fixy/session/Projection.h.
template <typename Peer>
struct Crash;

template <typename Peer, typename Label, typename Payload>
struct PeerMsg;

namespace detail {

template <VendorBackend V>
inline constexpr bool vendor_is_named_v = V != VendorBackend::None;

// The label that a message names is its peer and its label, without the
// payload.  Two branches of one choice that name the same label are not
// well-formed, as two branches of one Comm with the same label are not
// (fixy/session/Global.h).
namespace peer_message {
template <typename T>
struct label_of;
template <typename Peer, typename Label, typename Payload>
struct label_of<PeerMsg<Peer, Label, Payload>> {
    using type = PeerMsg<Peer, Label, void>;
};
template <typename T>
using label_of_t = typename label_of<T>::type;
}  // namespace peer_message

}  // namespace detail

// ── The registry ─────────────────────────────────────────────────────
//
// Send is covariant in its payload and Recv contravariant, so the
// relation is closed under duality: the payload variance flips with the
// shape.  The vendor value is invariant.  An order on it that let a
// Portable protocol stand for a pinned one would not be closed under
// duality, because the dual of a pinned endpoint is pinned to the same
// vendor.  The mint admission, not refinement, decides which vendor a
// context runs.
//
// The combinators stay open: a different header can register a new one.
// The payload rules are closed by the seal below.  A payload rule
// decides whether a payload can be sent, whether a branch is a label,
// and which label the branch names.  A rule that a later header adds
// would change those answers in some translation units and not in
// others, so every payload rule of the layer stands here, and a rule
// anywhere else stops the build.

namespace combinators {

inline constexpr ::foundation::algebra::transition::combinator send{
    .shape = ^^Send,
    .kind = ::foundation::algebra::transition::shape_kind::step,
    .direction = ::foundation::algebra::transition::polarity::output,
    .dual = ^^Recv,
    .payload_variance = ::foundation::algebra::transition::variance::covariant};

inline constexpr ::foundation::algebra::transition::combinator recv{
    .shape = ^^Recv,
    .kind = ::foundation::algebra::transition::shape_kind::step,
    .direction = ::foundation::algebra::transition::polarity::input,
    .dual = ^^Send,
    .payload_variance = ::foundation::algebra::transition::variance::contravariant};

// A choice and its dual name the same note template, so the dual of an
// Offer with a note is a Select with the same note, and duality is an
// involution on every registered combinator.
inline constexpr ::foundation::algebra::transition::combinator select{
    .shape = ^^Select,
    .kind = ::foundation::algebra::transition::shape_kind::choice,
    .direction = ::foundation::algebra::transition::polarity::output,
    .dual = ^^Offer,
    .annotation = ^^Sender};

inline constexpr ::foundation::algebra::transition::combinator offer{
    .shape = ^^Offer,
    .kind = ::foundation::algebra::transition::shape_kind::choice,
    .direction = ::foundation::algebra::transition::polarity::input,
    .dual = ^^Select,
    .annotation = ^^Sender};

inline constexpr ::foundation::algebra::transition::combinator loop{
    .shape = ^^Loop, .kind = ::foundation::algebra::transition::shape_kind::binder, .dual = ^^Loop};

inline constexpr ::foundation::algebra::transition::combinator loop_back{
    .shape = ^^Continue, .kind = ::foundation::algebra::transition::shape_kind::back, .dual = ^^Continue};

inline constexpr ::foundation::algebra::transition::combinator end{
    .shape = ^^End, .kind = ::foundation::algebra::transition::shape_kind::terminal, .dual = ^^End};

inline constexpr ::foundation::algebra::transition::combinator vendor_pinned{
    .shape = ^^VendorPinned,
    .kind = ::foundation::algebra::transition::shape_kind::wrapper,
    .dual = ^^VendorPinned,
    .value_variance = ::foundation::algebra::transition::variance::invariant,
    .value_admits = ^^detail::vendor_is_named_v};

// The crash label is a payload that no endpoint sends (rule 1) and that
// is no label a peer can send (rule 2).  fixy/session/Crash.h states why.
inline constexpr ::foundation::algebra::transition::payload_rule crash_label{
    .shape = ^^Crash, .is_sendable = false, .is_label = false};

inline constexpr ::foundation::algebra::transition::payload_rule peer_message{
    .shape = ^^PeerMsg, .label_key = ^^detail::peer_message::label_of_t};

inline constexpr ::foundation::algebra::transition::seal payload_rule_seal{
    .kind = ^^::foundation::algebra::transition::payload_rule, .count = 2};

}  // namespace combinators

namespace detail {

inline constexpr std::meta::info protocol_registry = ^^::fixy::session::combinators;

inline constexpr std::string_view unregistered_prefix = "fixy::session::diagnostic [Protocol_Unregistered_Combinator]: ";
inline constexpr std::string_view incoherent_prefix = "fixy::session::diagnostic [Protocol_Incoherent_Registration]: ";

// The refusal every primary template states first.  The head of P must
// be a registered combinator with a coherent registration.
template <typename P>
consteval void require_registered_head() {
    static_assert(::foundation::algebra::transition::is_registered(protocol_registry, ^^P),
                  ::foundation::algebra::transition::unregistered_message(unregistered_prefix, ^^P));
    static_assert(::foundation::algebra::transition::check_combinator(
                      protocol_registry, ::foundation::algebra::transition::shape_of(^^P))
                          .reason
                      == ::foundation::algebra::transition::incoherence::none,
                  ::foundation::algebra::transition::incoherent_message(
                      incoherent_prefix, ::foundation::algebra::transition::check_combinator(
                                             protocol_registry, ::foundation::algebra::transition::shape_of(^^P))));
}

template <typename P>
consteval bool head_is(std::meta::info shape) {
    return ::foundation::algebra::transition::head_shape(protocol_registry, ^^P) == shape;
}

}  // namespace detail

// ── Loop context ─────────────────────────────────────────────────────
//
// The traits indirection has one inhabitant while EpochCtx is absent:
// a LoopCtx is its own inner context.  It stays because the context
// axis is an extension point.  A wrapper that carries admission facts
// beside the loop specializes these three, and each Continue resolution
// stays correct.

template <typename LoopCtx>
struct session_loop_ctx_traits {
    using inner_loop_ctx = LoopCtx;
};

template <>
struct session_loop_ctx_traits<void> {
    using inner_loop_ctx = void;
};

template <typename LoopCtx>
using session_loop_ctx_inner_t = typename session_loop_ctx_traits<LoopCtx>::inner_loop_ctx;

template <typename LoopCtx, typename NewInnerLoopCtx>
struct session_loop_ctx_rebind_inner {
    using type = NewInnerLoopCtx;
};

template <typename LoopCtx, typename NewInnerLoopCtx>
using session_loop_ctx_rebind_inner_t = typename session_loop_ctx_rebind_inner<LoopCtx, NewInnerLoopCtx>::type;

// ── Shape traits ─────────────────────────────────────────────────────
//
// A recognizer compares the head shape of P, under its registered
// wrappers, with one combinator.  It needs no registration of P, so it
// answers false for a type that is not a protocol.

template <typename P>
struct is_send : std::bool_constant<detail::head_is<P>(^^Send)> {};

template <typename P>
struct is_recv : std::bool_constant<detail::head_is<P>(^^Recv)> {};

template <typename P>
struct is_select : std::bool_constant<detail::head_is<P>(^^Select)> {};

template <typename P>
struct is_offer : std::bool_constant<detail::head_is<P>(^^Offer)> {};

template <typename P>
struct is_loop : std::bool_constant<detail::head_is<P>(^^Loop)> {};

template <typename P>
struct is_end : std::bool_constant<detail::head_is<P>(^^End)> {};

template <typename P>
struct is_continue : std::bool_constant<detail::head_is<P>(^^Continue)> {};

template <typename P>
struct is_vendor_pinned : std::false_type {
    using protocol = P;
    static constexpr VendorBackend vendor_backend = VendorBackend::Portable;
};
template <VendorBackend V, typename P>
struct is_vendor_pinned<VendorPinned<V, P>> : std::true_type {
    using protocol = P;
    static constexpr VendorBackend vendor_backend = V;
};

template <typename P>
inline constexpr bool is_send_v = is_send<P>::value;
template <typename P>
inline constexpr bool is_recv_v = is_recv<P>::value;
template <typename P>
inline constexpr bool is_select_v = is_select<P>::value;
template <typename P>
inline constexpr bool is_offer_v = is_offer<P>::value;
template <typename P>
inline constexpr bool is_loop_v = is_loop<P>::value;
template <typename P>
inline constexpr bool is_end_v = is_end<P>::value;
template <typename P>
inline constexpr bool is_continue_v = is_continue<P>::value;
template <typename P>
inline constexpr bool is_vendor_pinned_v = is_vendor_pinned<P>::value;
template <typename P>
inline constexpr VendorBackend protocol_vendor_v = is_vendor_pinned<P>::vendor_backend;
template <typename P>
using protocol_inner_t = typename is_vendor_pinned<P>::protocol;

// A protocol head is a position a handle can occupy.  The test is
// negative, so a combinator that a different header registers is a
// head without an edit here.  Loop is the one non-head: the factory
// unrolls it and positions the handle at the body.
template <typename P>
inline constexpr bool is_head_v = !is_loop_v<P>;

// ── Terminality ──────────────────────────────────────────────────────
//
// The positions where a handle can be destroyed without a consume: a
// terminal combinator, under any wrappers.

template <typename P>
struct is_terminal_state;

template <typename P>
inline constexpr bool is_terminal_state_v = is_terminal_state<P>::value;

namespace detail {

template <typename P>
consteval bool terminal_state_of() {
    require_registered_head<P>();
    return ::foundation::algebra::transition::fold(protocol_registry, ^^P,
                                                   ::foundation::algebra::transition::terminal_algebra{}, 0,
                                                   ^^is_terminal_state_v);
}

}  // namespace detail

template <typename P>
struct is_terminal_state : std::bool_constant<detail::terminal_state_of<P>()> {};

// ── Empty choices ────────────────────────────────────────────────────
//
// A Select or an Offer with no label branch is not well-formed.  An
// empty Select has no branch to pick, and an empty Offer has no label
// the peer can send, so a handle there is stuck.  Under the branch rule
// an empty Select also refines every larger Select, and a substitute of
// that type never sends, so the session deadlocks.  is_well_formed
// therefore refuses it, and subtyping refuses it as an operand.
//
// This trait names the fault on its own, so handle construction can
// give it a specific diagnostic before the general one.  The walk
// covers the whole spine, because a handle reaches every position
// eventually.  A refusal at the top level only would let the misuse
// surface at the dead-end operation instead of at construction.

template <typename P>
struct is_empty_choice;

template <typename P>
inline constexpr bool is_empty_choice_v = is_empty_choice<P>::value;

namespace detail {

template <typename P>
consteval bool empty_choice_of() {
    require_registered_head<P>();
    return ::foundation::algebra::transition::fold(
        protocol_registry, ^^P, ::foundation::algebra::transition::empty_choice_algebra{protocol_registry}, 0,
        ^^is_empty_choice_v);
}

}  // namespace detail

template <typename P>
struct is_empty_choice : std::bool_constant<detail::empty_choice_of<P>()> {};

// ── Duality ──────────────────────────────────────────────────────────

template <typename P>
struct dual_of;

template <typename P>
using dual_of_t = typename dual_of<P>::type;

namespace detail {

template <typename P>
consteval std::meta::info dual_type_of() {
    require_registered_head<P>();
    if (!::foundation::algebra::transition::is_registered(protocol_registry, ^^P)) return ^^void;
    return ::foundation::algebra::transition::fold(
        protocol_registry, ^^P, ::foundation::algebra::transition::dual_algebra{protocol_registry}, 0, ^^dual_of_t);
}

}  // namespace detail

template <typename P>
struct dual_of {
    using type = typename[:detail::dual_type_of<P>():];
};

// The two endpoints of one channel must be duals, or the guarantee of
// deadlock freedom does not hold.
//
// The test asks for duality in both directions.  A channel pair has no
// primary side, so the answer must not depend on the order of the
// arguments.  Coherence makes duality an involution on each registered
// combinator, so the two directions agree there.  An explicit
// specialization of dual_of that is no involution makes them differ, and
// the test then refuses the pair.
template <typename P1, typename P2>
inline constexpr bool is_dual_v = std::is_same_v<dual_of_t<P1>, P2> && std::is_same_v<dual_of_t<P2>, P1>;

template <typename P1, typename P2>
consteval void ensure_dual() noexcept {
    static_assert(is_dual_v<P1, P2>, "fixy::session::diagnostic [Dual_Mismatch]: "
                                     "ensure_dual<P1, P2>(): the two endpoint protocols are NOT "
                                     "structural duals.  The Send of one side must face a Recv of the "
                                     "other side, a Select must face an Offer, Loop faces Loop, End "
                                     "faces End and Continue faces Continue.  Compare dual_of_t<P1> "
                                     "with P2: they must be the same type.  Without this duality the "
                                     "guarantee of deadlock freedom does NOT hold, and a runtime hang "
                                     "or a wrong reading of the transport bytes can occur.  Usual "
                                     "causes: (a) one side has a Send or Recv step that the other side "
                                     "does not have, (b) a Select faces a Select, or an Offer faces an "
                                     "Offer, (c) the branches of a Select and an Offer pair are not "
                                     "duals position by position.");
}

// ── Composition ──────────────────────────────────────────────────────
//
// Sequential composition replaces each End in P with Q.  Continue stays,
// because it marks a loop-back and not a protocol end, and it resolves
// against the LoopCtx when the handle steps.
//
// A Continue in Q that no Loop of Q binds names a Loop of the context.
// Where an End of P stands under a Loop of P, composition would put that
// Continue under the Loop of P, and the Continue would bind it: the
// substitution captures it.  Composition with a bare Continue would then
// turn each exit of a loop into a loop-back, and the protocol could
// never end.  Composition refuses that capture.  Without capture it
// keeps terminability: when P and Q can end from each position, so can
// the result.  foundation/algebra/Transition.h states the probe.

template <typename P, typename Q>
struct compose;

template <typename P, typename Q>
using compose_t = typename compose<P, Q>::type;

namespace detail {

// True when Q holds a Continue that no Loop of Q binds.  It is one
// constant per suffix, so a composition that reads it at each level of P
// computes it once.
template <typename Q>
inline constexpr bool suffix_is_open_v = ::foundation::algebra::transition::has_open_back(protocol_registry, ^^Q);

// True when composition of Q into P puts an open Continue of Q under a
// Loop of P.  The probe of P runs only for an open suffix.
template <typename P, typename Q>
consteval bool composition_captures() {
    if constexpr (!suffix_is_open_v<Q>) {
        return false;
    } else {
        return ::foundation::algebra::transition::has_bound_terminal(protocol_registry, ^^P);
    }
}

template <typename P, typename Q>
consteval std::meta::info compose_type_of() {
    require_registered_head<P>();
    if (!::foundation::algebra::transition::is_registered(protocol_registry, ^^P)) return ^^void;
    return ::foundation::algebra::transition::fold(
        protocol_registry, ^^P, ::foundation::algebra::transition::compose_algebra{protocol_registry, ^^Q}, 0,
        ^^compose_t);
}

}  // namespace detail

template <typename P, typename Q>
struct compose {
private:
    static constexpr bool captures = detail::composition_captures<P, Q>();
    static_assert(!captures,
                  "fixy::session::diagnostic [Compose_Captures_Continue]: compose_t<P, Q>: Q holds a Continue "
                  "that no Loop of Q binds, and an End of P that the composition replaces stands under a Loop of "
                  "P.  The Continue would bind that Loop, so the composed protocol would loop where P ends, and "
                  "it could lose every exit.  Put the Loop that the Continue means inside Q, or compose where no "
                  "Loop of P stands above the End.");

public:
    using type = typename[:captures ? ^^void : detail::compose_type_of<P, Q>():];
};

// Composition at one branch.  The walk passes the Send, Recv, Loop and
// VendorPinned nodes at the head of P, and at the first Select or Offer
// it composes Q into branch I alone.  The other branches do not change.
// Uniform composition, which appends Q to every branch, is compose_t.

template <typename P, std::size_t I, typename Q>
struct compose_at_branch;

template <typename P, std::size_t I, typename Q>
using compose_at_branch_t = typename compose_at_branch<P, I, Q>::type;

namespace detail {

template <typename P>
consteval ::foundation::algebra::transition::shape_kind spine_stop_kind() {
    return ::foundation::algebra::transition::first_stop_of_spine(protocol_registry, ^^P).entry.kind;
}

template <typename P>
consteval std::size_t spine_branch_count() {
    return ::foundation::algebra::transition::first_stop_of_spine(protocol_registry, ^^P).branches.size();
}

// True when composition of Q into branch I of the first choice of P puts
// an open Continue of Q under a Loop of P.  The Loops above the choice on
// the spine of P count, and so do the Loops inside the branch.
template <typename P, std::size_t I, typename Q>
consteval bool branch_composition_captures() {
    if constexpr (!suffix_is_open_v<Q>) {
        return false;
    } else {
        const ::foundation::algebra::transition::node stop =
            ::foundation::algebra::transition::first_stop_of_spine(protocol_registry, ^^P);
        if (stop.entry.kind != ::foundation::algebra::transition::shape_kind::choice || I >= stop.branches.size()) {
            return false;
        }
        return ::foundation::algebra::transition::has_bound_terminal(
            protocol_registry, stop.branches[I],
            ::foundation::algebra::transition::binders_above_first_stop(protocol_registry, ^^P));
    }
}

template <typename P, std::size_t I, typename Q>
consteval std::meta::info compose_at_branch_type_of() {
    return ::foundation::algebra::transition::fold(
        protocol_registry, ^^P,
        ::foundation::algebra::transition::compose_at_choice_algebra{protocol_registry, I, ^^Q, ^^compose_t}, 0,
        ^^compose_at_branch_t);
}

}  // namespace detail

template <typename P, std::size_t I, typename Q>
struct compose_at_branch {
private:
    static consteval bool check() {
        detail::require_registered_head<P>();
        return true;
    }
    static constexpr bool is_head_checked = check();
    static constexpr ::foundation::algebra::transition::shape_kind stop = detail::spine_stop_kind<P>();
    static constexpr bool reaches_choice = stop == ::foundation::algebra::transition::shape_kind::choice;
    static constexpr bool reaches_back = stop == ::foundation::algebra::transition::shape_kind::back;
    static constexpr bool index_fits = !reaches_choice || I < detail::spine_branch_count<P>();
    static constexpr bool captures = detail::branch_composition_captures<P, I, Q>();

    static_assert(reaches_choice || reaches_back,
                  "fixy::session::diagnostic [Branch_Compose_No_Choice]: "
                  "compose_at_branch_t<P, I, Q>: the walk along the spine of P reached a terminal and found "
                  "no Select<Bs...> or Offer<Bs...> to compose at.  Composition at one branch needs a choice "
                  "combinator on the spine of P, under its Send, Recv, Loop and VendorPinned nodes.  For "
                  "uniform composition, where each End becomes Q, use compose_t<P, Q>.");
    static_assert(!reaches_back, "fixy::session::diagnostic [Branch_Compose_No_Choice]: "
                                 "compose_at_branch_t<P, I, Q>: the walk along the spine of P reached "
                                 "Continue and found no Select or Offer.  Continue is a loop-back "
                                 "marker, not a choice point.");
    static_assert(index_fits, "fixy::session::diagnostic [Branch_Compose_Index_Out_Of_Range]: "
                              "compose_at_branch_t<P, I, Q>: the branch index I is out of range for the "
                              "first Select or Offer on the spine of P.  The count of branches does not "
                              "include the Sender<Role> note: an Offer<Sender<R>, B0, B1> has 2 branches, "
                              "not 3.");
    static_assert(!captures,
                  "fixy::session::diagnostic [Compose_Captures_Continue]: compose_at_branch_t<P, I, Q>: Q holds "
                  "a Continue that no Loop of Q binds, and an End of branch I stands under a Loop of P, on the "
                  "spine above the choice or inside the branch.  The Continue would bind that Loop, so the "
                  "branch would loop where it ends.  Put the Loop that the Continue means inside Q.");

public:
    using type = typename[:is_head_checked && reaches_choice && index_fits && !captures
                              ? detail::compose_at_branch_type_of<P, I, Q>()
                              : ^^void:];
};

// ── Well-formedness ──────────────────────────────────────────────────
//
// A protocol is well-formed when:
//
//   1. each Continue has an enclosing Loop, and a step or a choice lies
//      between the two.  A Continue with no Loop above it has no state
//      to loop back to, and a Continue with nothing before it inside
//      its Loop unfolds for ever;
//   2. no Loop body is a terminal state.  A Loop over a terminal never
//      reaches its Continue;
//   3. no Send sends a payload that a payload registration marks as not
//      sendable;
//   4. no VendorPinned names VendorBackend::None;
//   5. each Select and each Offer has a label branch or more, puts its
//      label branches before each branch that is no label, matches each
//      branch uniquely, names a label key on each label branch or on
//      none, and gives each label key its own label word.
//      foundation/algebra/Transition.h states the five rules, and
//      ensure_choices_well_formed below names the rule that a choice
//      breaks.
//
// LoopCtx is void outside a loop.  A Loop type as LoopCtx, the form the
// handle carries, means inside one loop after its first step.  The fold
// passes a transition scope to the trait for each child.

template <typename P, typename LoopCtx = void>
struct is_well_formed;

namespace detail {

template <typename P, typename Scope>
inline constexpr bool well_formed_at_v = is_well_formed<P, Scope>::value;

template <typename LoopCtx>
consteval ::foundation::algebra::transition::well_formed_algebra::position position_of() {
    constexpr std::meta::info context = std::meta::dealias(^^LoopCtx);
    if constexpr (std::meta::has_template_arguments(context)
                  && std::meta::template_of(context) == ^^::foundation::algebra::transition::scope) {
        return {LoopCtx::depth, LoopCtx::guarded};
    } else if constexpr (std::is_void_v<session_loop_ctx_inner_t<LoopCtx>>) {
        return {0, true};
    } else {
        return {1, true};
    }
}

template <typename P, typename LoopCtx>
consteval bool well_formed_of() {
    require_registered_head<P>();
    return ::foundation::algebra::transition::fold(
        protocol_registry, ^^P,
        ::foundation::algebra::transition::well_formed_algebra{protocol_registry, ^^is_terminal_state_v},
        position_of<LoopCtx>(), ^^well_formed_at_v);
}

}  // namespace detail

template <typename P, typename LoopCtx>
struct is_well_formed : std::bool_constant<detail::well_formed_of<P, LoopCtx>()> {};

template <typename P>
inline constexpr bool is_well_formed_v = is_well_formed<P>::value;

namespace detail {

template <typename P>
consteval std::string_view choice_fault_message() {
    const ::foundation::algebra::transition::choice_verdict verdict =
        ::foundation::algebra::transition::first_faulty_choice(protocol_registry, ^^P);
    std::string text = "fixy::session::diagnostic [Protocol_Choice_Ill_Formed]: the choice ";
    text += verdict.choice == std::meta::info{} ? std::string_view{"(none)"}
                                                : std::meta::display_string_of(verdict.choice);
    text += " is not well-formed: ";
    text += ::foundation::algebra::transition::choice_fault_name(verdict.fault);
    text += ".";
    return std::define_static_string(text);
}

}  // namespace detail

// Names the first choice of P that breaks a rule of the section on
// branches and labels of foundation/algebra/Transition.h, and the rule.
// Complexity: the sum of O(b²) over the choices of P.
template <typename P>
consteval void ensure_choices_well_formed() noexcept {
    detail::require_registered_head<P>();
    static_assert(::foundation::algebra::transition::first_faulty_choice(detail::protocol_registry, ^^P).fault
                      == ::foundation::algebra::transition::choice_fault::none,
                  detail::choice_fault_message<P>());
}

// Every registration of this header is coherent.  A registration that a
// different header adds is checked where a query first meets it.
static_assert(::foundation::algebra::transition::check_registry(detail::protocol_registry).reason
                  == ::foundation::algebra::transition::incoherence::none,
              "fixy::session::diagnostic [Protocol_Incoherent_Registration]: a registration in "
              "fixy::session::combinators is incoherent.");

// The seal counts every payload rule of the registry.  A rule that stands
// before this header, in a namespace that a different header opened
// first, makes the count differ here.
static_assert(::foundation::algebra::transition::read_seal(detail::protocol_registry,
                                                           ^^::foundation::algebra::transition::payload_rule)
                      .fault
                  == ::foundation::algebra::transition::seal_fault::none,
              "fixy::session::diagnostic [Protocol_Payload_Rule_Outside_Seal]: fixy::session::combinators holds a "
              "payload rule that its seal does not count.  Every payload rule stands in fixy/session/Protocol.h.");

}  // namespace fixy::session
