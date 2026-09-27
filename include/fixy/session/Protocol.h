#pragma once

// The session-type protocol DSL.  A protocol is a type built from Send,
// Recv, Select, Offer, Loop, Continue, End, VendorPinned, Delegate and
// Accept.  The two endpoints of one channel agree when their protocols
// are duals.  Stop, Commit, Roll and Abort are combinators too, and no
// plain protocol holds them: Stop is the runtime type of a crashed
// endpoint (fixy/session/Crash.h), and the three others are the
// checkpoint primitives (fixy/session/Checkpoint.h).
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
// Each query reads the registry and fails closed.  A type that is not a
// registered combinator stops the build with a message that names it.
// The registry is sealed: every combinator of the layer is registered
// in this header, and a registration anywhere else stops the build.
//
// No trait here is a template that a user can specialize.  A class
// spelling such as dual_of or is_well_formed is an alias template, and a
// boolean spelling such as is_well_formed_v is a concept, so each
// specialization of one is a compile error.  The fold recurses in place,
// so no template of this layer answers for a child node either.
//
// This header holds the type level only.  Nothing here has a runtime
// representation, so nothing here depends on the abandonment policy.
// The handle and that policy are in fixy/session/Handle.h.
//
// The EpochCtx context wrapper of the ported source is not here.  No
// production file uses it, so the port drops it.  The lattices that it
// reads are in foundation: foundation/algebra/lattices/StrongCounterLattice.h
// holds EpochLattice and GenerationLattice.  A loop context has four
// forms, and one function reads them (the section on the loop context).

#include <foundation/algebra/Transition.h>
#include <foundation/algebra/lattices/VendorLattice.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>
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
using offer_sender = std::type_identity<typename OfferType::sender>;

template <typename OfferType>
using offer_sender_t = typename OfferType::sender;

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

// The delegation heads.  Delegate<T, K> sends an endpoint of protocol T
// and then continues as K.  Accept<T, K> receives an endpoint of
// protocol T and then continues as K.  The endpoint itself travels, not
// the view of its peer, so the dual of each head keeps T as it is.
// fixy/session/Delegate.h states what a mint does with a protocol that
// holds one of them.
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

// The runtime type of a crashed endpoint.  fixy/session/Crash.h states
// its rules: no protocol written at design time holds it, and no handle
// stands at it.
struct Stop {};

// The checkpoint primitives of fixy/session/Checkpoint.h.  Commit<K>
// takes a checkpoint and continues with K, Roll returns both parties to
// their checkpoints, and Abort returns both parties to the start.  Each
// is a label of a choice that the two parties exchange, and only the
// checkpoint mint admits a protocol that holds one.
template <typename K>
struct Commit {
    using next = K;
};

struct Roll {};

struct Abort {};

// The payloads that the registry below has a rule for.  Each is defined
// in the header of its layer: Crash in fixy/session/Crash.h, and PeerMsg
// and Labelled in fixy/session/Projection.h.
template <typename Peer>
struct Crash;

template <typename Peer, typename Label, typename Payload>
struct PeerMsg;

template <typename Label, typename Payload>
struct Labelled;

// A payload that carries a live endpoint of another session.  It is
// defined in fixy/session/Delegate.h.  The payload walk of
// fixy/session/Payload.h and the payload order of
// fixy/session/Subtype.h name it here.
template <typename InnerProto, typename Resource, typename Policy, typename InnerPS>
class DelegatedSession;

namespace detail {

template <VendorBackend V>
inline constexpr bool vendor_is_named_v = V != VendorBackend::None;

// The label key of a message is its label alone, the key of the binary
// view.  Each endpoint names the other role as the peer, so a key that
// held the peer would differ between a choice and its dual, and the two
// would put different words on one wire.  With the label alone, a
// PeerMsg choice, its dual and its stripped view put one word on the
// wire.  The peer stays exact in the payload order (the axiom
// peer_message of fixy/session/Subtype.h), so a message to another peer
// refines nothing.  A choice of the paper has one peer, so two branches
// that name one label are not well-formed, whatever their peers, as two
// branches of one Comm with the same label are not (fixy/session/Global.h).
namespace peer_message {
template <typename T>
struct label_of;
template <typename Peer, typename Label, typename Payload>
struct label_of<PeerMsg<Peer, Label, Payload>> {
    using type = Labelled<Label, void>;
};
template <typename T>
using label_of_t = typename label_of<T>::type;

// The sender of a received message signals the Offer that its keyed
// Recv stands for.
template <typename T>
struct note_of;
template <typename Peer, typename Label, typename Payload>
struct note_of<PeerMsg<Peer, Label, Payload>> {
    using type = Sender<Peer>;
};
template <typename T>
using note_of_t = typename note_of<T>::type;
}  // namespace peer_message

// The label key of a message of the binary view is its label without the
// payload.  A binary view has one peer, so the label alone tells its
// branches apart, as the peer and the label do in the local type that the
// view strips.
namespace labelled {
template <typename T>
struct label_of;
template <typename Label, typename Payload>
struct label_of<Labelled<Label, Payload>> {
    using type = Labelled<Label, void>;
};
template <typename T>
using label_of_t = typename label_of<T>::type;
}  // namespace labelled

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
// Two seals below close the combinators and the payload rules.  A
// combinator that a later header adds would give its shape an answer in
// some translation units and none in others, and a second registration
// of a shape would give that shape two answers.  A payload rule decides
// whether a payload can be sent, whether a branch is a label, and which
// label the branch names, so a rule that a later header adds would
// change those answers in some translation units and not in others.
// Every registration of the layer therefore stands here, and a
// registration anywhere else stops the build.

namespace combinators {

// A keyed Send is a Select of one branch, and a keyed Recv is an Offer of
// one branch (foundation/algebra/Transition.h, section on branches and
// labels).
inline constexpr ::foundation::algebra::transition::combinator send{
    .shape = ^^Send,
    .kind = ::foundation::algebra::transition::shape_kind::step,
    .direction = ::foundation::algebra::transition::polarity::output,
    .dual = ^^Recv,
    .payload_variance = ::foundation::algebra::transition::variance::covariant,
    .keyed_choice = ^^Select};

inline constexpr ::foundation::algebra::transition::combinator recv{
    .shape = ^^Recv,
    .kind = ::foundation::algebra::transition::shape_kind::step,
    .direction = ::foundation::algebra::transition::polarity::input,
    .dual = ^^Send,
    .payload_variance = ::foundation::algebra::transition::variance::contravariant,
    .keyed_choice = ^^Offer};

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

// A delegation head is a step, and the protocol of the endpoint is its
// payload.  The payload is invariant, so this layer states no refinement
// between two delegated protocols at a head.  The payload order of
// fixy/session/Subtype.h orders the DelegatedSession that a Send carries.
// The delegated protocol becomes a session of its own, so it is
// well-formed outside every Loop, and an empty choice in it is found.
inline constexpr ::foundation::algebra::transition::combinator delegate{
    .shape = ^^Delegate,
    .kind = ::foundation::algebra::transition::shape_kind::step,
    .direction = ::foundation::algebra::transition::polarity::output,
    .dual = ^^Accept,
    .payload_is_protocol = true};

inline constexpr ::foundation::algebra::transition::combinator accept{
    .shape = ^^Accept,
    .kind = ::foundation::algebra::transition::shape_kind::step,
    .direction = ::foundation::algebra::transition::polarity::input,
    .dual = ^^Delegate,
    .payload_is_protocol = true};

// Stop is a terminal that absorbs a suffix: a crashed endpoint never
// resumes, so composition keeps it.  Its dual is itself, and refinement
// relates it only to itself (rule Sub-stop of fixy/session/Crash.h).
inline constexpr ::foundation::algebra::transition::combinator stop{
    .shape = ^^Stop,
    .kind = ::foundation::algebra::transition::shape_kind::terminal,
    .dual = ^^Stop,
    .absorbs_suffix = true,
    .is_plain = false};

// Commit is a marker: it takes a checkpoint and continues, and its dual
// commits at the same label.  A Roll or an Abort never falls through to
// what follows it, so composition keeps it, as it keeps a crashed
// endpoint.  No handle stands at one of the three: the checkpoint handle
// does the primitive in the step that exchanges its label.
inline constexpr ::foundation::algebra::transition::combinator commit{
    .shape = ^^Commit,
    .kind = ::foundation::algebra::transition::shape_kind::marker,
    .dual = ^^Commit,
    .is_plain = false};

inline constexpr ::foundation::algebra::transition::combinator roll{
    .shape = ^^Roll,
    .kind = ::foundation::algebra::transition::shape_kind::terminal,
    .dual = ^^Roll,
    .absorbs_suffix = true,
    .is_plain = false};

inline constexpr ::foundation::algebra::transition::combinator restart{
    .shape = ^^Abort,
    .kind = ::foundation::algebra::transition::shape_kind::terminal,
    .dual = ^^Abort,
    .absorbs_suffix = true,
    .is_plain = false};

inline constexpr ::foundation::algebra::transition::seal combinator_seal{
    .kind = ^^::foundation::algebra::transition::combinator, .count = 14};

// The crash label is a payload that no endpoint sends (rule 1) and that
// is no label a peer can send (rule 2).  fixy/session/Crash.h states why.
inline constexpr ::foundation::algebra::transition::payload_rule crash_label{
    .shape = ^^Crash, .is_sendable = false, .is_label = false};

// A keyed Recv of a message from Peer is the Offer that Peer signals, so
// it takes the note Sender<Peer>, as the projection writes that Offer.
inline constexpr ::foundation::algebra::transition::payload_rule peer_message{
    .shape = ^^PeerMsg,
    .label_key = ^^detail::peer_message::label_of_t,
    .input_note = ^^detail::peer_message::note_of_t};

// A message of the binary view names a label key too, so a choice of the
// binary view is keyed, and the handle sends the label word.
inline constexpr ::foundation::algebra::transition::payload_rule labelled{
    .shape = ^^Labelled, .label_key = ^^detail::labelled::label_of_t};

inline constexpr ::foundation::algebra::transition::seal payload_rule_seal{
    .kind = ^^::foundation::algebra::transition::payload_rule, .count = 3};

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

// ── The members of a node ────────────────────────────────────────────
//
// A reader of this layer reads a nested member of a combinator, such as
// `next` or `body`.  The member claims of a node state the value that
// its template arguments give to each such member, so an explicit
// specialization that lies about one is refused at the mint.  The list of
// members is pinned by test/fixy/test_session_node_members.cpp, which
// walks the members of each registered combinator.

inline constexpr std::string_view specialized_prefix = "fixy::session::diagnostic [Protocol_Specialized_Combinator]: ";

// The node reader of this layer.  It claims each member that a reader
// reads: message_type and next of a Send or a Recv, delegated_proto and
// next of a delegation head, body of a Loop, next of a Commit, protocol
// and vendor_backend of a pin, which also derives from the protocol that
// it pins, and branch_count, branches_tuple and the sender of an Offer of
// a choice.  End, Continue, Stop, Roll and Abort hold no member.  The
// children are each node that a handle can step to: the next step, each
// branch of a choice (the crash branches among them), the body of a loop,
// the protocol under a pin, and the protocol that a delegation head
// carries.  The payload of a step is a child too, so the members of a
// crash label and of a keyed message are claimed.
[[nodiscard]] consteval ::foundation::algebra::transition::node_members session_node_members(std::meta::info type) {
    using ::foundation::algebra::transition::member_claim;
    ::foundation::algebra::transition::node_members result{};
    // A payload with a rule of the registry is read by its members too:
    // the peer of a crash label, and the peer, label and payload of a
    // keyed message.
    if (std::meta::is_type(type) && std::meta::has_template_arguments(type)) {
        const std::meta::info family = std::meta::template_of(type);
        const auto arguments = std::meta::template_arguments_of(type);
        if (family == ^^Crash) {
            result.is_node = true;
            result.claims = {member_claim{"peer", arguments[0]}};
            return result;
        }
        if (family == ^^PeerMsg) {
            result.is_node = true;
            result.claims = {member_claim{"peer", arguments[0]}, member_claim{"label", arguments[1]},
                             member_claim{"payload", arguments[2]}};
            return result;
        }
        if (family == ^^Labelled) {
            result.is_node = true;
            result.claims = {member_claim{"label", arguments[0]}, member_claim{"payload", arguments[1]}};
            return result;
        }
    }
    const ::foundation::algebra::transition::node view = ::foundation::algebra::transition::decompose(protocol_registry, type);
    if (!view.is_registered) return result;
    result.is_node = true;
    const std::meta::info shape = view.entry.shape;
    if (shape == ^^Send || shape == ^^Recv) {
        result.claims = {member_claim{"message_type", view.payload}, member_claim{"next", view.next}};
    } else if (shape == ^^Delegate || shape == ^^Accept) {
        result.claims = {member_claim{"delegated_proto", view.payload}, member_claim{"next", view.next}};
    } else if (shape == ^^Loop) {
        result.claims = {member_claim{"body", view.next}};
    } else if (shape == ^^Commit) {
        result.claims = {member_claim{"next", view.next}};
    } else if (shape == ^^VendorPinned) {
        result.claims = {member_claim{"protocol", view.next}, member_claim{"vendor_backend", view.value}};
        result.bases = {view.next};
    } else if (shape == ^^Select || shape == ^^Offer) {
        result.claims = {member_claim{"branch_count", std::meta::reflect_constant(view.branches.size())},
                         member_claim{"branches_tuple", std::meta::substitute(^^std::tuple, view.branches)}};
        if (shape == ^^Offer) {
            const std::meta::info sender = view.annotation == std::meta::info{}
                                               ? ^^AnonymousPeer
                                               : std::meta::dealias(std::meta::template_arguments_of(view.annotation)[0]);
            result.claims.push_back(member_claim{"sender", sender});
        }
    }
    if (view.payload != std::meta::info{}) result.children.push_back(view.payload);
    if (view.next != std::meta::info{}) result.children.push_back(view.next);
    for (const std::meta::info branch : view.branches) result.children.push_back(branch);
    return result;
}

// The refusal of a protocol with a node whose members lie.  The static
// assertion stops the build, so no program that compiles reads the answer
// after it.  The answer is true, so a gate that holds this clause adds no
// second error to the refusal.
template <typename P>
consteval bool require_agreeing_members() {
    constexpr std::meta::info disagreeing =
        ::foundation::algebra::transition::first_disagreeing_node(&session_node_members, ^^P);
    static_assert(disagreeing == std::meta::info{},
                  ::foundation::algebra::transition::disagreeing_message(
                      specialized_prefix, disagreeing == std::meta::info{} ? ^^P : disagreeing));
    return true;
}

// The refusal of every walk that reads the children of a node.  Each node
// of the spine of P must be registered, and the message names the first
// one that is not.  A payload and a value are not nodes.  It returns false
// after that refusal, so the walk stops there and adds no second error.
// Then each node must keep the members that its arguments give.  The walks
// read the arguments, so a walk goes on after that refusal and adds no
// error.
template <typename P>
consteval bool require_registered_spine() {
    constexpr std::meta::info missing = ::foundation::algebra::transition::first_unregistered(protocol_registry, ^^P);
    static_assert(missing == std::meta::info{},
                  ::foundation::algebra::transition::unregistered_message(
                      unregistered_prefix, missing == std::meta::info{} ? ^^P : missing));
    if constexpr (missing == std::meta::info{}) {
        require_registered_head<P>();
        return require_agreeing_members<P>();
    } else {
        return false;
    }
}

// The recognizer of every shape trait.  It is a function over
// reflections, not a template, so no specialization reaches it.
[[nodiscard]] consteval bool head_is(std::meta::info type, std::meta::info shape) {
    return ::foundation::algebra::transition::head_shape(protocol_registry, type) == shape;
}

}  // namespace detail

// ── Loop context ─────────────────────────────────────────────────────
//
// A handle carries a loop context, which has one of four forms:
//
//   void                          outside a loop
//   Loop<Body>                    after the first step of a loop
//   PermLoopFrame<Loop, EntryPS>  the same, when the loop starts with a
//                                 permission set that each iteration must
//                                 give back
//   session_brand<Brand, Inner>   one of the forms above, lent by an
//                                 owning entry point to a body
//
// One function over reflections reads each form and refuses every other
// type, and the aliases below splice its answer.  An alias and a function
// that is not a template take no specialization, so no program can send
// Continue to a body that the loop does not hold.  The body comes from the
// template argument of the Loop, never from a member, so a specialization
// of Loop cannot change it either.

namespace detail {

// The loop context of a handle that an owning entry point lends to a
// body.  fixy/session/Handle.h states what the brand closes.  Each form
// is read from its template arguments, so no form has a member.
template <typename Brand, typename InnerLoopCtx = void>
struct session_brand {};

// The loop context of a loop that starts with the permission set EntryPS.
// fixy/session/Handle.h builds it and checks the set at each Continue.
template <typename LoopType, typename EntryPS>
struct PermLoopFrame {};

// Called during constant evaluation only for a loop context of no known
// form, so the call is the diagnostic.
void a_loop_context_has_no_known_form() noexcept;

[[nodiscard]] consteval bool is_instance_of(std::meta::info type, std::meta::info family) {
    return ::foundation::algebra::transition::shape_of(type) == family;
}

// The Loop that an inner loop context stands for, or void for void.
// Any other type is refused.
[[nodiscard]] consteval std::meta::info loop_of_inner(std::meta::info inner) {
    const std::meta::info type = std::meta::dealias(inner);
    if (type == ^^void || is_instance_of(type, ^^Loop)) return type;
    if (is_instance_of(type, ^^PermLoopFrame)) {
        const std::meta::info loop = std::meta::dealias(std::meta::template_arguments_of(type)[0]);
        if (is_instance_of(loop, ^^Loop)) return loop;
    }
    a_loop_context_has_no_known_form();
    return ^^void;
}

// The inner loop context: void, a Loop or a frame, with every brand taken
// away.
[[nodiscard]] consteval std::meta::info inner_loop_ctx_of(std::meta::info loop_ctx) {
    const std::meta::info type = std::meta::dealias(loop_ctx);
    if (is_instance_of(type, ^^session_brand)) return inner_loop_ctx_of(std::meta::template_arguments_of(type)[1]);
    static_cast<void>(loop_of_inner(type));
    return type;
}

// The loop context with its inner context replaced.  A brand stays around
// the new inner context.
[[nodiscard]] consteval std::meta::info rebind_inner_loop_ctx(std::meta::info loop_ctx, std::meta::info new_inner) {
    const std::meta::info type = std::meta::dealias(loop_ctx);
    if (is_instance_of(type, ^^session_brand)) {
        const auto arguments = std::meta::template_arguments_of(type);
        return std::meta::substitute(^^session_brand, {arguments[0], rebind_inner_loop_ctx(arguments[1], new_inner)});
    }
    static_cast<void>(loop_of_inner(type));
    static_cast<void>(loop_of_inner(new_inner));
    return std::meta::dealias(new_inner);
}

// The body of the loop that a loop context or a Loop stands for, read
// from the template argument of the Loop.
[[nodiscard]] consteval std::meta::info loop_body_of(std::meta::info loop_ctx) {
    const std::meta::info loop = loop_of_inner(inner_loop_ctx_of(loop_ctx));
    if (loop == ^^void) a_loop_context_has_no_known_form();
    return std::meta::dealias(std::meta::template_arguments_of(loop)[0]);
}

template <typename LoopCtx>
using loop_body_t = [:loop_body_of(^^LoopCtx):];

}  // namespace detail

template <typename LoopCtx>
using session_loop_ctx_inner_t = [:detail::inner_loop_ctx_of(^^LoopCtx):];

template <typename LoopCtx, typename NewInnerLoopCtx>
using session_loop_ctx_rebind_inner_t = [:detail::rebind_inner_loop_ctx(^^LoopCtx, ^^NewInnerLoopCtx):];

// ── Shape traits ─────────────────────────────────────────────────────
//
// A recognizer compares the head shape of P, under its registered
// wrappers, with one combinator.  It needs no registration of P, so it
// answers false for a type that is not a protocol.

template <typename P>
using is_send = std::bool_constant<detail::head_is(^^P, ^^Send)>;

template <typename P>
using is_recv = std::bool_constant<detail::head_is(^^P, ^^Recv)>;

template <typename P>
using is_select = std::bool_constant<detail::head_is(^^P, ^^Select)>;

template <typename P>
using is_offer = std::bool_constant<detail::head_is(^^P, ^^Offer)>;

template <typename P>
using is_loop = std::bool_constant<detail::head_is(^^P, ^^Loop)>;

template <typename P>
using is_end = std::bool_constant<detail::head_is(^^P, ^^End)>;

template <typename P>
using is_continue = std::bool_constant<detail::head_is(^^P, ^^Continue)>;

template <typename P>
concept is_send_v = detail::head_is(^^P, ^^Send);
template <typename P>
concept is_recv_v = detail::head_is(^^P, ^^Recv);
template <typename P>
concept is_select_v = detail::head_is(^^P, ^^Select);
template <typename P>
concept is_offer_v = detail::head_is(^^P, ^^Offer);
template <typename P>
concept is_loop_v = detail::head_is(^^P, ^^Loop);
template <typename P>
concept is_end_v = detail::head_is(^^P, ^^End);
template <typename P>
concept is_continue_v = detail::head_is(^^P, ^^Continue);

namespace detail {

// The vendor of a VendorPinned at the head of P, and the protocol under
// it.  Every other protocol is Portable, and it is its own protocol.
template <typename P>
struct vendor_view : std::false_type {
    using protocol = P;
    static constexpr VendorBackend vendor_backend = VendorBackend::Portable;
};
template <VendorBackend V, typename P>
struct vendor_view<VendorPinned<V, P>> : std::true_type {
    using protocol = P;
    static constexpr VendorBackend vendor_backend = V;
};

}  // namespace detail

template <typename P>
using is_vendor_pinned = detail::vendor_view<P>;
template <typename P>
concept is_vendor_pinned_v = detail::vendor_view<P>::value;
template <typename P>
inline constexpr VendorBackend protocol_vendor_v = detail::vendor_view<P>::vendor_backend;
template <typename P>
using protocol_inner_t = typename detail::vendor_view<P>::protocol;

// A Send or a Recv whose payload names a label key, a PeerMsg or a
// Labelled.  Outside a choice it is the Select or the Offer of that one
// branch.  Its message is the label word below, and then the value of its
// payload when the payload is not void (fixy/session/Handle.h).
template <typename P>
concept is_keyed_step_v = ::foundation::algebra::transition::is_keyed_step_type(detail::protocol_registry, ^^P);

namespace detail {

// The label word that a keyed step sends or expects.  The handle reads
// this function and not step_wire_word_v, so a specialization of the
// public spelling changes only what its author reads, never the wire.
[[nodiscard]] consteval std::uint64_t step_wire_word_of(std::meta::info step) {
    return ::foundation::algebra::transition::wire_word_of_step(protocol_registry, step).value;
}

}  // namespace detail

template <typename P>
    requires is_keyed_step_v<P>
inline constexpr std::uint64_t step_wire_word_v = detail::step_wire_word_of(^^P);

// A protocol head is a position a handle can occupy.  Loop is the one
// non-head: the factory unrolls it and positions the handle at the body.
template <typename P>
concept is_head_v = !detail::head_is(^^P, ^^Loop);

// ── Terminality ──────────────────────────────────────────────────────
//
// The positions where a handle can be destroyed without a consume: a
// terminal combinator, under any wrappers.

namespace detail {

template <typename P>
consteval bool terminal_state_of() {
    require_registered_head<P>();
    return ::foundation::algebra::transition::fold(protocol_registry, ^^P,
                                                   ::foundation::algebra::transition::terminal_algebra{}, 0);
}

}  // namespace detail

template <typename P>
using is_terminal_state = std::bool_constant<detail::terminal_state_of<P>()>;

template <typename P>
concept is_terminal_state_v = detail::terminal_state_of<P>();

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
// surface at the dead-end operation instead of at construction.  A
// delegated protocol becomes a session of its own, where an empty choice
// leaves its holder stuck, so the walk looks into it too (the payload
// kind of the delegation heads in the registry).

namespace detail {

template <typename P>
consteval bool empty_choice_of() {
    if (!require_registered_spine<P>()) return false;
    return ::foundation::algebra::transition::fold(
        protocol_registry, ^^P, ::foundation::algebra::transition::empty_choice_algebra{protocol_registry}, 0);
}

}  // namespace detail

template <typename P>
using is_empty_choice = std::bool_constant<detail::empty_choice_of<P>()>;

template <typename P>
concept is_empty_choice_v = detail::empty_choice_of<P>();

// ── Duality ──────────────────────────────────────────────────────────

namespace detail {

template <typename P>
consteval std::meta::info dual_type_of() {
    if (!require_registered_spine<P>()) return ^^void;
    return ::foundation::algebra::transition::fold(
        protocol_registry, ^^P, ::foundation::algebra::transition::dual_algebra{protocol_registry}, 0);
}

template <typename P>
struct dual_view {
    using type = typename[:dual_type_of<P>():];
};

}  // namespace detail

template <typename P>
using dual_of = detail::dual_view<P>;

template <typename P>
using dual_of_t = typename detail::dual_view<P>::type;

// ── The canonical spelling ───────────────────────────────────────────
//
// A keyed Send and the Select of that one branch are one type, and so are
// a keyed Recv and the Offer of that one branch with the note of its
// sender.  canonical_t writes each such choice as its step, so two
// spellings of one protocol have one canonical spelling.

namespace detail {

template <typename P>
struct canonical_of;

}  // namespace detail

template <typename P>
using canonical_t = typename detail::canonical_of<P>::type;

namespace detail {

// A node that the registry does not know keeps its spelling.
template <typename P>
consteval std::meta::info canonical_type_of() {
    return ::foundation::algebra::transition::fold(
        protocol_registry, ^^P, ::foundation::algebra::transition::canonical_algebra{protocol_registry}, 0);
}

template <typename P>
struct canonical_of {
    using type = typename[:canonical_type_of<P>():];
};

}  // namespace detail

// The two endpoints of one channel must be duals, or the guarantee of
// deadlock freedom does not hold.
//
// The test asks for duality in both directions.  A channel pair has no
// primary side, so the answer must not depend on the order of the
// arguments.  Coherence makes duality an involution on each registered
// combinator, so the two directions agree.  The test compares canonical
// spellings, so a keyed step faces the choice of that one branch.
template <typename P1, typename P2>
concept is_dual_v = std::is_same_v<canonical_t<dual_of_t<P1>>, canonical_t<P2>>
                 && std::is_same_v<canonical_t<dual_of_t<P2>>, canonical_t<P1>>;

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
    if (!require_registered_spine<P>()) return ^^void;
    return ::foundation::algebra::transition::fold(
        protocol_registry, ^^P, ::foundation::algebra::transition::compose_algebra{protocol_registry, ^^Q}, 0);
}

template <typename P, typename Q>
struct compose_view {
private:
    static constexpr bool captures = composition_captures<P, Q>();
    static_assert(!captures,
                  "fixy::session::diagnostic [Compose_Captures_Continue]: compose_t<P, Q>: Q holds a Continue "
                  "that no Loop of Q binds, and an End of P that the composition replaces stands under a Loop of "
                  "P.  The Continue would bind that Loop, so the composed protocol would loop where P ends, and "
                  "it could lose every exit.  Put the Loop that the Continue means inside Q, or compose where no "
                  "Loop of P stands above the End.");

public:
    using type = typename[:captures ? ^^void : compose_type_of<P, Q>():];
};

}  // namespace detail

template <typename P, typename Q>
using compose = detail::compose_view<P, Q>;

template <typename P, typename Q>
using compose_t = typename detail::compose_view<P, Q>::type;

// Composition at one branch.  The walk passes the steps, binders,
// wrappers and markers at the head of P, and at the first Select or
// Offer it composes Q into branch I alone.  The other branches do not
// change.  Uniform composition, which appends Q to every branch, is
// compose_t.

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
        ::foundation::algebra::transition::compose_at_choice_algebra{protocol_registry, I, ^^Q}, 0);
}

template <typename P, std::size_t I, typename Q>
struct compose_at_branch_view {
private:
    static consteval bool check() { return require_registered_spine<P>(); }
    static constexpr bool is_head_checked = check();
    static constexpr ::foundation::algebra::transition::shape_kind stop = spine_stop_kind<P>();
    static constexpr bool reaches_choice = stop == ::foundation::algebra::transition::shape_kind::choice;
    static constexpr bool reaches_back = stop == ::foundation::algebra::transition::shape_kind::back;
    static constexpr bool index_fits = !reaches_choice || I < spine_branch_count<P>();
    static constexpr bool captures = branch_composition_captures<P, I, Q>();

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
                              ? compose_at_branch_type_of<P, I, Q>()
                              : ^^void:];
};

}  // namespace detail

template <typename P, std::size_t I, typename Q>
using compose_at_branch = detail::compose_at_branch_view<P, I, Q>;

template <typename P, std::size_t I, typename Q>
using compose_at_branch_t = typename detail::compose_at_branch_view<P, I, Q>::type;

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
//      none, gives each label key its own label word, and in a keyed
//      choice makes each label branch its label step, with no Loop and
//      no VendorPinned above it.  foundation/algebra/Transition.h states
//      the six rules, and ensure_choices_well_formed below names the
//      rule that a choice breaks;
//   6. the delegated protocol of each Delegate and each Accept is
//      well-formed outside every Loop.  The endpoint travels to another
//      participant, so a Continue in the delegated protocol cannot name a
//      Loop of the carrier;
//   7. no combinator is one that no plain protocol holds: Stop, Commit,
//      Roll and Abort.
//
// A Send or a Recv of a PeerMsg or a Labelled is keyed.  Outside a choice
// it is the Select or the Offer of that one branch, and the handle puts
// its label word on the wire (fixy/session/Handle.h).
//
// LoopCtx is void outside a loop.  A Loop type as LoopCtx, the form the
// handle carries, means inside one loop after its first step.

namespace detail {

template <typename LoopCtx>
consteval ::foundation::algebra::transition::well_formed_algebra::position position_of() {
    if constexpr (std::is_void_v<session_loop_ctx_inner_t<LoopCtx>>) {
        return {0, true};
    } else {
        return {1, true};
    }
}

template <typename P, typename LoopCtx>
consteval bool well_formed_of() {
    require_registered_head<P>();
    static_cast<void>(require_agreeing_members<P>());
    return ::foundation::algebra::transition::fold(
        protocol_registry, ^^P, ::foundation::algebra::transition::well_formed_algebra{protocol_registry},
        position_of<LoopCtx>());
}

}  // namespace detail

template <typename P, typename LoopCtx = void>
using is_well_formed = std::bool_constant<detail::well_formed_of<P, LoopCtx>()>;

template <typename P>
concept is_well_formed_v = detail::well_formed_of<P, void>();

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

// The seal counts every combinator of the registry.  A registration that
// stands before this header, in a namespace that a different header
// opened first, makes the count differ here.  A registration after this
// header makes it differ at the next read of a shape.
static_assert(::foundation::algebra::transition::read_seal(detail::protocol_registry,
                                                           ^^::foundation::algebra::transition::combinator)
                      .fault
                  == ::foundation::algebra::transition::seal_fault::none,
              "fixy::session::diagnostic [Protocol_Combinator_Outside_Seal]: fixy::session::combinators holds a "
              "combinator registration that its seal does not count.  Every combinator of the session layer is "
              "registered in fixy/session/Protocol.h.");

namespace detail {

// True when every registration of the registry is coherent.  It reads no
// shape while the seal is broken, so a registration outside the seal
// gives the one diagnostic above.  The registry is a parameter, so the
// compiler cannot fold the read before the test of the seal.
[[nodiscard]] consteval bool registry_is_coherent(std::meta::info registry) {
    namespace tr = ::foundation::algebra::transition;
    if (tr::read_seal(registry, ^^tr::combinator).fault != tr::seal_fault::none) return true;
    return tr::check_registry(registry).reason == tr::incoherence::none;
}

}  // namespace detail

static_assert(detail::registry_is_coherent(detail::protocol_registry),
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
