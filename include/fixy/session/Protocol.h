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
// of an Offer:
//
//   Offer<Sender<Alice>, Recv<Msg, End>, Recv<Crash<Alice>, Recovery>>
//
// A two-party Offer needs no note, because "the peer" is clear.  A
// multiparty local protocol can hold Offers that different roles
// signal, and crash analysis must know which role signals an Offer.
// The note is not a branch.
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

namespace detail {

template <VendorBackend V>
inline constexpr bool vendor_is_named_v = V != VendorBackend::None;

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

inline constexpr ::foundation::algebra::transition::combinator select{
    .shape = ^^Select,
    .kind = ::foundation::algebra::transition::shape_kind::choice,
    .direction = ::foundation::algebra::transition::polarity::output,
    .dual = ^^Offer};

// The dual of an Offer is a Select, which has no note, so the dual
// drops the Sender note.  From the dual endpoint the sender of the
// Offer is the local role, and the note has no purpose on an
// internal choice.  Duality is therefore not an involution on an Offer
// with a note.
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

// True when dual(dual(P)) is P.  The answer is the round trip itself,
// so it is exact for each registered combinator, and a type that is not
// a registered combinator stops the build.  It is false on an Offer
// with a Sender note, whose dual drops the note.
template <typename P>
struct is_dual_involutive : std::bool_constant<std::is_same_v<dual_of_t<dual_of_t<P>>, P>> {};

template <typename P>
inline constexpr bool is_dual_involutive_v = is_dual_involutive<P>::value;

// The two endpoints of one channel must be duals, or the guarantee of
// deadlock freedom does not hold.
//
// The disjunction is necessary.  A channel pair has no primary side, so
// the test must not depend on the order of the arguments, and the
// simpler `dual_of_t<P1> == P2` does.  On an Offer with a Sender note
// the forward direction drops the note, and the reverse direction
// cannot restore it.

template <typename P1, typename P2>
inline constexpr bool is_dual_v = std::is_same_v<dual_of_t<P1>, P2> || std::is_same_v<P1, dual_of_t<P2>>;

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

template <typename P, typename Q>
struct compose;

template <typename P, typename Q>
using compose_t = typename compose<P, Q>::type;

namespace detail {

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
    using type = typename[:detail::compose_type_of<P, Q>():];
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

public:
    using type = typename[:is_head_checked && reaches_choice && index_fits
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
//      label branches before each branch that is no label, and matches
//      each branch uniquely.  The position of a label branch is the
//      label that the handle sends, so a Select or an Offer in another
//      order is another protocol.  foundation/algebra/Transition.h
//      states the three rules.
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

// Every registration of this header is coherent.  A registration that a
// different header adds is checked where a query first meets it.
static_assert(::foundation::algebra::transition::check_registry(detail::protocol_registry).reason
                  == ::foundation::algebra::transition::incoherence::none,
              "fixy::session::diagnostic [Protocol_Incoherent_Registration]: a registration in "
              "fixy::session::combinators is incoherent.");

}  // namespace fixy::session
