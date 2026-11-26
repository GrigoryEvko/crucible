#pragma once

// The admission of a session: the gates that a protocol, a permission set,
// a Resource and a context pass before a session exists.
//
// fixy/session/Handle.h builds a handle only for what these gates admit,
// and the mints of fixy/session/Mint.h ask the same gates.

#include <fixy/SelfContained.h>
#include <fixy/concurrent/PayloadRow.h>
#include <fixy/concurrent/PermissionFork.h>
#include <fixy/session/Classification.h>
#include <fixy/session/Flow.h>
#include <fixy/session/Protocol.h>
#include <fixy/session/Registry.h>
#include <fixy/session/Watch.h>
#include <fixy/session/Wire.h>

#include <foundation/Pinned.h>
#include <foundation/Transfer.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/PermSet.h>
#include <foundation/permissions/Permission.h>
#include <foundation/reflect/Anchor.h>
#include <foundation/reflect/Instance.h>
#include <foundation/reflect/TypeComponents.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace fixy::session {

namespace detail {

// ── The ownership delta of one message ───────────────────────────────
//
// These names are the only place where the handle reads what a message
// does to the permission set.  fixy/session/Flow.h computes the delta
// from the walk of fixy/session/Classification.h over every component of
// the payload: a send needs what the payload takes, and the matching
// receive gains it.  A payload that the walk refuses, for example a token
// behind a pointer, is refused here.
// Each name is a concept, so no user specialization changes a step.
template <typename PS, typename T>
concept handle_admits_send_v = SendablePayload<T, PS>;

template <typename PS, typename T>
concept handle_admits_recv_v = ReceivablePayload<T, PS>;

template <typename PS, typename T>
using perm_set_after_send_t = ::fixy::session::perm_set_after_send_t<PS, T>;

template <typename PS, typename T>
using perm_set_after_recv_t = ::fixy::session::perm_set_after_recv_t<PS, T>;

// True when a handle at End can close with this set.  An open loan, a
// LentOut or a BorrowedIn, is a promise that the protocol did not keep,
// so the set must hold none.  An owned tag travelled in a payload, and
// its token is held by the value that carried it.
template <typename PS>
concept perm_set_admits_close_v = !perm_set_has_open_loan_v<PS>;

// True when the set is a PermSet with no tag, so a loop frame is the Loop
// itself.  The answer is a function at namespace scope that is not a
// template, so no translation unit can specialize it.
[[nodiscard]] consteval bool perm_set_is_empty(std::meta::info set) {
    const std::meta::info type = std::meta::dealias(set);
    return std::meta::has_template_arguments(type)
        && std::meta::template_of(type) == (^^::foundation::permissions::PermSet)
        && std::meta::template_arguments_of(type).empty();
}

// ── The loop frame ────────────────────────────────────────────────────
//
// A Continue must see the permission set that the loop saw at its
// entry.  If one iteration changed the set, the next iteration would
// start with a different set, and a permission would be lost or
// duplicated once per iteration.  The frame records the entry set beside
// the loop.  With an empty set the frame is the Loop itself, so a handle
// without permissions has the same loop context as before.  The frame
// type, PermLoopFrame, stands in fixy/session/Protocol.h beside the other
// forms of a loop context.

// The entry set of a loop context: the second argument of a frame, and
// the empty set for any other form.  The form is checked, so no type of an
// unknown form gives an entry set.
[[nodiscard]] consteval std::meta::info entry_perm_set_of(std::meta::info loop_ctx) {
    const std::meta::info inner = inner_loop_ctx_of(loop_ctx);
    if (is_instance_of(inner, ^^PermLoopFrame)) return std::meta::dealias(std::meta::template_arguments_of(inner)[1]);
    return ^^::foundation::permissions::EmptyPermSet;
}

template <typename Frame>
using loop_entry_perm_set_t = [:entry_perm_set_of(^^Frame):];

template <typename LoopType, typename PS>
using loop_frame_t = std::conditional_t<perm_set_is_empty(^^PS), LoopType, PermLoopFrame<LoopType, PS>>;

// ── The session brand ────────────────────────────────────────────────
//
// An entry point that owns the handle (with_session, mint_forked_channel)
// takes back the handle that its body returns.  If the body could return
// any End handle over the same Resource, it could mint a new session,
// walk that one to End, and return it in place of the handle it was
// given.  The brand closes this.  The owning entry point puts the type
// of the body in the loop context of every handle of its session, and it
// accepts back only a handle with that brand.  No public mint makes a
// branded handle, so the returned handle is one that the body received.
//
// The brand, session_brand, is a form of loop context, so it stands in
// fixy/session/Protocol.h with the other forms.

// The brand of a loop context, or void for a context with no brand.
[[nodiscard]] consteval std::meta::info brand_of(std::meta::info loop_ctx) {
    const std::meta::info type = std::meta::dealias(loop_ctx);
    if (is_instance_of(type, ^^session_brand)) return std::meta::dealias(std::meta::template_arguments_of(type)[0]);
    return ^^void;
}

// True when the loop context carries the brand Brand.  A context with no
// brand carries the brand void.
template <typename LoopCtx, typename Brand>
concept carries_brand = brand_of(^^LoopCtx) == std::meta::dealias(^^Brand);

}  // namespace detail

// ── The admission of a protocol ──────────────────────────────────────
//
// The admission gate for handle construction, expressed as a concept
// rather than as body static_asserts alone.  A body static_assert is
// not visible to SFINAE: overload resolution accepts the signature for
// any Proto and the failure only appears at instantiation, where a
// requires-expression further up the stack cannot observe it.
// Downstream concepts that ask whether a handle can be minted need that
// answer, so the checks live in the signature.

namespace detail {

// The number of distinct roles that a local type and the loop around it
// name as peers, together.  The loop is the Loop that the loop context
// stands for, read through each form of fixy/session/Protocol.h: a session
// brand and a loop frame are not nodes of the registry, so the walk of the
// context itself would see no peer of the body.  A context of no known form
// stops the build, so no unread context counts as one with no peer.  The
// walk is collect_named_peers of fixy/session/Flow.h.  Complexity: quadratic
// in the number of peer names that the walk finds.
[[nodiscard]] consteval std::size_t distinct_peer_count(std::meta::info protocol, std::meta::info loop_ctx) {
    stack<std::meta::info> distinct{};
    collect_named_peers(protocol, distinct);
    const std::meta::info loop = loop_of_inner(inner_loop_ctx_of(loop_ctx));
    if (loop != ^^void) collect_named_peers(loop, distinct);
    return distinct.top;
}

}  // namespace detail

// A local type that names two peers or more is the projection of a
// multiparty protocol onto one role.  Whether it runs safely depends on
// the global type and on the network model of the carrier
// (fixy/session/Network.h).  A mint of fixy/session/Mint.h sees neither,
// so it refuses such a local type.  A multiparty binding asks
// CarrierImplements of fixy/session/Network.h with the global type first.
template <typename Proto>
concept NamesAtMostOnePeer = detail::distinct_peer_count(^^Proto, ^^void) <= 1;

// An empty choice is not well-formed either.  The empty-choice clause
// comes first so that the refusal of such a protocol names that fault
// and not the general one.  A protocol that hands an endpoint of a
// session to a peer of that session is refused too
// (DelegatesToNoOwnPeer in fixy/session/Flow.h).
template <typename Proto>
concept WellFormedRunnableProtocol =
    !is_empty_choice_v<Proto> && is_well_formed_v<Proto> && DelegatesToNoOwnPeer<Proto> && NamesAtMostOnePeer<Proto>;

namespace detail {

// The permission flow of a whole protocol from a starting set PS.  The
// handle checks each step when the step is compiled, and a branch that no
// run selects is never compiled.  This walk visits every branch, so an arm
// that leaves a loan open, or sends a region that the set does not hold,
// is refused at the mint even when the program never selects that arm.
//
// loop_ps is the set at the entry of the innermost Loop, or void outside
// every Loop.  A head that the walk does not know is refused.  The walk is
// a function at namespace scope that is not a template, so no translation
// unit can specialize a step to close a flow that leaves a loan open.
//
// The walk keeps each answer of the evaluation, so branches that share a
// continuation walk it one time.  The answers live in the one evaluation,
// so the walk nests no instantiation, and a step into a message child is
// one call deep, as a walk with no answers is.
//
// Complexity: one visit for each distinct triple of a node, a set and a
// loop set, and a linear search of the answers at each visit.  So the walk
// is quadratic in the distinct triples of the protocol.

// One answer of the walk: whether the flow of `protocol` from the set `ps`
// closes, with `loop_ps` at the entry of the innermost Loop.  Each of the
// three has its aliases removed.
struct FlowAnswer {
    std::meta::info protocol{};
    std::meta::info ps{};
    std::meta::info loop_ps{};
    bool closes = false;
};

// The flow of one triple.  A triple that `answered` holds gives its answer
// with no walk.
[[nodiscard]] consteval bool walk_flow(std::meta::info protocol, std::meta::info ps, std::meta::info loop_ps,
                                       stack<FlowAnswer>& answered) {
    const std::meta::info node = std::meta::dealias(protocol);
    const std::meta::info set = std::meta::dealias(ps);
    const std::meta::info loop_set = std::meta::dealias(loop_ps);
    for (const FlowAnswer& known : answered) {
        if (known.protocol == node && known.ps == set && known.loop_ps == loop_set) return known.closes;
    }
    namespace tr = ::fixy::session::transition;
    // The walk reads each node through the registry, so the note of a
    // choice is no branch, and a combinator is known here when the fold
    // knows it.  A combinator that is not plain belongs to an extension that
    // the handle does not run (a crash stop, a checkpoint primitive), and
    // the walk refuses it.
    // The walk reads the node now, with no cell of the node for each type.
    // The answers of the walk already keep each node of one walk, and the
    // read of a cell costs more than the read of the node here.
    const tr::node view = tr::detail::decompose_now(protocol_registry, node);
    bool closes = false;
    if (view.is_registered && view.entry.is_plain) {
        switch (view.entry.kind) {
            case tr::shape_kind::terminal:
                closes = std::meta::extract<bool>(std::meta::substitute(^^perm_set_admits_close_v, {set}));
                break;
            case tr::shape_kind::back:
                // The parentheses end the reflection, so && is not a part of
                // its type.
                closes = loop_set != (^^void)&&::foundation::permissions::perm_set_equal(set, loop_set);
                break;
            case tr::shape_kind::step:
                // A step whose payload is a protocol hands off or takes an
                // endpoint, and no handle has a step for it.
                if (view.entry.payload_is_protocol) break;
                if (view.entry.direction == tr::polarity::output) {
                    closes =
                        std::meta::extract<bool>(std::meta::substitute(^^handle_admits_send_v, {set, view.payload}))
                        && walk_flow(view.next, std::meta::substitute(^^perm_set_after_send_t, {set, view.payload}),
                                     loop_set, answered);
                } else if (view.entry.direction == tr::polarity::input) {
                    closes =
                        std::meta::extract<bool>(std::meta::substitute(^^handle_admits_recv_v, {set, view.payload}))
                        && walk_flow(view.next, std::meta::substitute(^^perm_set_after_recv_t, {set, view.payload}),
                                     loop_set, answered);
                }
                break;
            case tr::shape_kind::choice:
                // Each branch closes from the same sets.  The walk stops at
                // the first branch that does not close.
                closes = true;
                for (const std::meta::info branch : view.branches)
                    closes = closes && walk_flow(branch, set, loop_set, answered);
                break;
            case tr::shape_kind::binder:
                closes = walk_flow(view.next, set, set, answered);
                break;
            case tr::shape_kind::wrapper:
                closes = walk_flow(view.next, set, loop_set, answered);
                break;
            case tr::shape_kind::named:
                // The protocol of a name is closed, so a Continue in it binds
                // a Loop in it, and the walk enters it outside every Loop.
                closes = walk_flow(view.next, set, ^^void, answered);
                break;
            // The walk reads no mark, so a mark does not close.
            case tr::shape_kind::marker:
            default:
                break;
        }
    }
    push(answered, FlowAnswer{node, set, loop_set, closes});
    return closes;
}

[[nodiscard]] consteval bool permission_flow_closes(std::meta::info protocol, std::meta::info ps,
                                                    std::meta::info loop_ps) {
    stack<FlowAnswer> answered{};
    return walk_flow(protocol, ps, loop_ps, answered);
}

}  // namespace detail

// True when every path of Proto, from the set PS, sends only what the set
// holds, receives no second owner of a region, returns each loop to the
// set of its entry, and reaches End with no open loan.
template <typename Proto, typename PS>
concept PermissionFlowCloses = detail::permission_flow_closes(^^Proto, ^^PS, ^^void);

// ── The transfer of each payload ─────────────────────────────────────
//
// A transport can give a payload to a thread that runs after the sender
// ends, as a channel can give its value.  So each payload of a protocol
// moves to another thread and holds no borrow at any depth (Sendable and
// SendableOwned of foundation/Transfer.h), as each value of a channel does.
// The walk reads each Send and each Recv, on each branch and in each loop
// body, and the value of each keyed message.  The row gate of the mints asks
// this gate first, and each rewind asks it too.  So a payload that holds a
// borrow stops the build here, and the rosters of
// fixy/concurrent/PayloadRow.h do not decide it.  Each send door and the
// receive door of fixy/session/Handle.h ask the same of their value.

namespace detail {

// True when a value of the type moves to another thread and holds no
// borrow.  Complexity: two walks of foundation/Transfer.h, each linear in
// the distinct parts of the type.
[[nodiscard]] consteval bool payload_transfers(std::meta::info payload) {
    namespace transfer = ::foundation::transfer;
    return transfer::holds(payload, transfer::Property::Send)
        && transfer::holds(payload, transfer::Property::SendOwned);
}

}  // namespace detail

// A value of P can travel on a session: it moves to another thread, and it
// holds no borrow.
template <class P>
concept PayloadTransfers = detail::payload_transfers(^^P);

namespace detail::walks {

// The first payload of a protocol that does not transfer, or a null
// reflection.  A keyed step writes the value of its message, so the walk
// reads that value.  A step whose payload is a protocol has no handle step,
// and the walk does not read it.  The walk does not enter the protocol of a
// delegated endpoint, because the mint of that endpoint reads it.
struct transfer_algebra {
    using result = std::meta::info;
    using context = int;

    template <class Child>
    consteval result terminal(const tr::node&, context, const Child&) const {
        return {};
    }
    template <class Child>
    consteval result back(const tr::node&, context, const Child&) const {
        return {};
    }
    template <class Child>
    consteval result step(const tr::node& view, context ctx, const Child& child) const {
        if (!view.entry.payload_is_protocol) {
            const std::meta::info value = written_value_of(view.payload);
            if (!std::meta::is_void_type(value) && !payload_transfers(value)) return value;
        }
        return child(view.next, ctx);
    }
    template <class Child>
    consteval result choice(const tr::node& view, context ctx, const Child& child) const {
        for (const std::meta::info branch : view.branches) {
            const result refused = child(branch, ctx);
            if (refused != std::meta::info{}) return refused;
        }
        return {};
    }
    template <class Child>
    consteval result binder(const tr::node& view, context ctx, const Child& child) const {
        return child(view.next, ctx);
    }
    template <class Child>
    consteval result wrapper(const tr::node& view, context ctx, const Child& child) const {
        return child(view.next, ctx);
    }
    template <class Child>
    consteval result marker(const tr::node& view, context ctx, const Child& child) const {
        return child(view.next, ctx);
    }
    template <class Child>
    consteval result named(const tr::node& view, context ctx, const Child& child) const {
        return child(view.next, ctx);
    }
    consteval result unregistered(const tr::node&, context) const { return {}; }

private:
    // The value that a step writes: the value of a keyed message, or the
    // payload itself.
    static consteval std::meta::info written_value_of(std::meta::info payload) {
        const payload_lookup keyed = message_rule_of(payload);
        if (!keyed.is_found) return std::meta::dealias(payload);
        return rule_argument_of(payload, keyed.entry.value_argument);
    }
};

}  // namespace detail::walks

namespace detail {

// True when each payload of the protocol transfers.  The first payload that
// does not stops the build with its refusal.  Complexity: one fold, linear
// in the distinct nodes, with the two transfer walks of each payload.
[[nodiscard]] consteval bool protocol_payloads_transfer(std::meta::info protocol) {
    require_acyclic_names(protocol);
    const std::meta::info refused = walks::tr::fold(protocol_registry, protocol, walks::transfer_algebra{}, 0);
    if (refused == std::meta::info{}) return true;
    throw std::meta::exception(
        u8"fixy::session::diagnostic [Payload_Not_Owned]: a payload of the protocol cannot move to another thread, or it "
        u8"holds a borrow (Sendable and SendableOwned of foundation/Transfer.h).  A transport can give the payload to a "
        u8"thread that runs after the sender ends.  Send a value that owns each part that it holds.  The protocol of "
        u8"the refusal names the payload.",
        refused);
}

}  // namespace detail

// Each payload of Proto travels on a session (PayloadTransfers).
template <class Proto>
concept ProtocolPayloadsTransfer = detail::protocol_payloads_transfer(^^Proto);

// ── Rewinding a session ──────────────────────────────────────────────
//
// A rewind opens a new session at a position inside a protocol, for
// example at the checkpoint that a rollback returns to.  The gate asks
// the same of that session as the context-free mint asks of a session
// from its start, so a rewind gives nothing that closing the End handle
// and minting a new session does not give:
//
//   - The loop context is none, or a plain Loop that is a runnable
//     protocol by itself.  A session brand is refused, so no rewind makes
//     a handle that a callback entry accepts back.
//   - R, under one Loop binder, is a runnable protocol.  Each Continue in
//     R binds to that binder, and the handle binds it to the loop context.
//   - R and the loop context together name one peer at most, as the
//     local type of a binary session does.
//   - Every path moves no permission, because the set is empty.
//   - Each payload of R and of the loop context travels on a session
//     (ProtocolPayloadsTransfer above).
//   - The handle at End belongs to no session that a callback entry owns,
//     because that entry takes its handle back.

// True when a session can start at position R in the loop context LoopCtx.
// The concept names each condition itself, and each operand of a
// conjunction or a disjunction is asked only when the operands before it
// leave the answer open.  No function template decides the answer, so no
// translation unit can specialize it.
template <typename R, typename LoopCtx>
concept ResumablePosition = (std::is_void_v<LoopCtx> && !std::is_same_v<R, Continue> && WellFormedRunnableProtocol<R>
                             && PermissionFlowCloses<R, ::foundation::permissions::EmptyPermSet>)
                         || (is_loop_v<LoopCtx> && WellFormedRunnableProtocol<LoopCtx>
                             && PermissionFlowCloses<LoopCtx, ::foundation::permissions::EmptyPermSet>
                             && (std::is_same_v<R, Continue>
                                 || (WellFormedRunnableProtocol<Loop<R>>
                                     && PermissionFlowCloses<Loop<R>, ::foundation::permissions::EmptyPermSet>
                                     && detail::distinct_peer_count(^^R, ^^LoopCtx) <= 1)));

// The gate of HandleFactory::rewind.  A rewind names a protocol of its own,
// and no context, so the gate asks the transfer of each payload of R and of
// the loop that R continues, as each mint asks it (ProtocolPayloadsTransfer).
template <typename R, typename LoopCtx, typename EndLoopCtx>
concept RewindableTo = ResumablePosition<R, LoopCtx> && detail::carries_brand<EndLoopCtx, void>
                    && ProtocolPayloadsTransfer<R> && (std::is_void_v<LoopCtx> || ProtocolPayloadsTransfer<LoopCtx>);

namespace detail {

// The loop context of the peer of a channel end: none for none, and the
// dual Loop for a Loop.
template <typename LoopCtx>
struct dual_loop_ctx {
    using type = dual_of_t<LoopCtx>;
};
template <>
struct dual_loop_ctx<void> {
    using type = void;
};

template <typename LoopCtx>
using dual_loop_ctx_t = typename dual_loop_ctx<LoopCtx>::type;

}  // namespace detail

// ── The Resource ─────────────────────────────────────────────────────
//
// The Resource is what a handle stores at runtime.  The concept stops a
// handle from outliving what it points at, and it stops a second holder
// from reaching the channel of a live session.
//
// A value type is safe when the handle owns it: their lifetimes coincide.
// An lvalue reference to a Pinned object is safe because a Pinned object
// cannot be moved, so its address is stable for its whole lifetime and
// the caller only has to outlive the handles.  An lvalue reference to a
// non-Pinned object is refused: a move of the referent relocates it and
// every live handle dangles.  An rvalue reference is refused because it
// binds to a temporary that dies before the handle is used.
//
// A value Resource that reaches state outside itself has one holder.  A
// copy of it is a second channel to the peer: the holder could send the
// copy in a message, or build a second handle on it, and write outside
// the protocol.  This is the one-holder rule of per-message resources in
// Actris: a channel end is an exclusive resource, never a duplicable one.
// So a Resource that can be copied must be fixy::SelfContained, which
// reads every base and member, private ones too, and names the part that
// reaches out.  A raw pointer, a function pointer and a
// std::reference_wrapper are copyable and reach out, so they are refused.
// A Resource that reaches a channel holds a MoveOnlyResource member,
// which deletes its copy and keeps it an aggregate.
//
// The reach is conservative in the way SelfContained is: a member that
// the walk cannot account for counts as a reach.  Complexity: linear in
// the distinct types reached from the Resource.

// A member that makes the Resource that holds it move-only.  It is empty,
// and [[no_unique_address]] gives it no byte.
struct MoveOnlyResource {
    constexpr MoveOnlyResource() noexcept = default;
    constexpr MoveOnlyResource(MoveOnlyResource&&) noexcept = default;
    constexpr MoveOnlyResource& operator=(MoveOnlyResource&&) noexcept = default;
    MoveOnlyResource(const MoveOnlyResource&) =
        delete("[Resource_Copy] a Resource that reaches a channel has one holder.  A copy is a second channel");
    MoveOnlyResource& operator=(const MoveOnlyResource&) =
        delete("[Resource_Copy] a Resource that reaches a channel has one holder.  A copy is a second channel");
    ~MoveOnlyResource() = default;
};

// True when a value of the type can be duplicated: by construction from
// a copy, or by assignment from one.  The answer is a function at
// namespace scope that is not a template, so no translation unit can
// specialize it.
[[nodiscard]] consteval bool resource_is_copyable(std::meta::info resource) {
    return std::meta::is_copy_constructible_type(resource) || std::meta::is_copy_assignable_type(resource);
}

// A value Resource: owned by the handle, and either self-contained or
// impossible to copy.
template <typename Resource>
concept OwnedSessionResource =
    !std::is_reference_v<Resource> && (!resource_is_copyable(^^Resource) || ::fixy::SelfContained<Resource>);

// A reference Resource: an lvalue reference to a Pinned object.
template <typename Resource>
concept PinnedSessionResource =
    std::is_lvalue_reference_v<Resource>
    && std::derived_from<std::remove_reference_t<Resource>, ::foundation::Pinned<std::remove_reference_t<Resource>>>;

template <typename Resource>
concept SessionResource = OwnedSessionResource<Resource> || PinnedSessionResource<Resource>;

// ── The poles of a channel ───────────────────────────────────────────
//
// A pole of a channel is a class that names its channel with a public
// member type channel_type, as each handle of fixy/concurrent and of
// fixy/channels does, or a class that binds its channel with a member of the
// type foundation::ChannelBinding, as each handle of the MetaLog does.  The
// binding itself moves no value, so it is no pole.  The channel of a pole is
// its channel_type, or the channel of its binding.
//
// The protocol of a channel is one value for the channel.  A mint of one
// end takes the protocol of that end from its caller, and the peer end of
// the same channel is minted apart, so no mint of one end can see the two
// protocols.  The two ends of one channel could then run protocols that
// are not duals.  So no mint that takes a protocol from its caller admits
// a Resource that reaches a pole: the Resource itself, a base, a member, an
// element, or a type that a pointer, a reference or a template argument
// names (the walk of foundation/reflect/TypeComponents.h).  A pole enters a
// session only through SessionMintDoor::open_pole of fixy/session/Handle.h,
// which takes the two protocols of the channel from the pole table of the
// channel (below, after the relation of the two sides).
//
// The walk reads each class that it reaches.  A class that the Resource
// holds by value is complete.  A class that the Resource reaches through a
// pointer, a reference or a template argument, the walk reads with the
// instantiating read of foundation/reflect/TypeComponents.h.  That read
// instantiates a specialization of a class template, so the walk sees a pole
// that is a specialization there too.  The walk tells a ChannelBinding by its
// template, before it reads the binding.  A class that the walk cannot read
// stops the build: a class with no definition in the unit, or a
// specialization of a template with no definition in the unit.  Such a class
// could be a pole in a unit that defines it and no pole in this unit, and the
// two units would then hold different answers.  A class that holds a channel
// and neither names it with channel_type nor binds it is no pole, so this
// rule does not see it either.

namespace detail {

// True when the type is a specialization of foundation::ChannelBinding.  The
// test reads the template of the type, so it instantiates nothing.
[[nodiscard]] consteval bool is_channel_binding(std::meta::info type) {
    if (!std::meta::is_type(type) || !std::meta::has_template_arguments(type)) return false;
    const std::meta::info family = std::meta::template_of(type);
    return std::meta::parent_of(family) == ^^::foundation&& std::meta::identifier_of(family) == "ChannelBinding";
}

// The channel that the type names with channel_type, or null.
[[nodiscard]] consteval std::meta::info named_channel_of(std::meta::info type) {
    const std::meta::info member = ::foundation::reflect::member_named(type, "channel_type");
    return member != std::meta::info{} && std::meta::is_type(member) ? std::meta::dealias(member) : std::meta::info{};
}

// The channel of a pole: the channel that it names with channel_type, or the
// channel of its ChannelBinding member.  Null for a class that is no pole.
// The caller reads the members of a complete class only.  Complexity: linear
// in the members of the class.
[[nodiscard]] consteval std::meta::info pole_channel_of(std::meta::info type) {
    if (!std::meta::is_class_type(type) || is_channel_binding(type)) return {};
    const std::meta::info named = named_channel_of(type);
    if (named != std::meta::info{}) return named;
    namespace rf = ::foundation::reflect;
    for (const rf::TypeNode& part : rf::member_components_of(rf::TypeNode{type, true})) {
        if (is_channel_binding(part.type)) return named_channel_of(part.type);
    }
    return {};
}

// True when the type is a pole of a channel.  The answer is a function at
// namespace scope that is not a template, so no translation unit can
// specialize it.
[[nodiscard]] consteval bool is_channel_pole(std::meta::info type) {
    return pole_channel_of(type) != std::meta::info{};
}

// Called in a constant evaluation only when the walk of a Resource reaches a
// class that it cannot read, so the call is the diagnostic.
void a_resource_reaches_a_class_that_the_pole_walk_cannot_read() noexcept;

// True for a class or union whose members the walk cannot read: the
// instantiating read left it incomplete.
[[nodiscard]] consteval bool is_unreadable_class(const ::foundation::reflect::TypeNode& node) {
    const std::meta::info type = ::foundation::reflect::bare_type(node.type);
    return (std::meta::is_class_type(type) || std::meta::is_union_type(type)) && !node.may_read_members;
}

// True when the walk of foundation/reflect/TypeComponents.h, with the
// instantiating read, reaches a pole of a channel from the type.  The walk
// stops the build at a class that it cannot read.  It visits each node once,
// as first_component_satisfying of that header does.  The function is no
// template, so GCC builds its body once in each includer.  Complexity:
// linear in the distinct nodes, times the scan of the visited nodes.
[[nodiscard]] consteval bool reaches_a_channel_pole(std::meta::info root) {
    namespace rf = ::foundation::reflect;
    namespace tr = ::fixy::session::transition;
    constexpr rf::SpecializationRead read = rf::SpecializationRead::Instantiating;
    tr::stack<rf::TypeNode> pending{};
    tr::stack<rf::TypeNode> visited{};
    tr::push(pending, rf::TypeNode{rf::bare_type(root), true});
    while (pending.top != 0) {
        --pending.top;
        const rf::TypeNode node = pending.items[pending.top];
        bool was_visited = false;
        for (const rf::TypeNode& seen : visited) {
            if (seen.type == node.type && (seen.may_read_members || !node.may_read_members)) {
                was_visited = true;
                break;
            }
        }
        if (was_visited) continue;
        tr::push(visited, node);
        if (is_channel_binding(node.type)) return true;
        if (is_unreadable_class(node)) a_resource_reaches_a_class_that_the_pole_walk_cannot_read();
        if (node.may_read_members && is_channel_pole(node.type)) return true;
        for (const rf::TypeNode& part : rf::member_components_of(node, read))
            tr::push(pending, part);
        for (const rf::TypeNode& part : rf::argument_components_of(node, read))
            tr::push(pending, part);
    }
    return false;
}

}  // namespace detail

// The Resource is a pole of a channel, held by value.
template <typename Resource>
concept ChannelPole = !std::is_reference_v<Resource> && detail::is_channel_pole(^^Resource);

// The channel of a pole.
template <ChannelPole Pole>
using pole_channel_t = typename[:detail::pole_channel_of(^^Pole):];

// The Resource is a pole, or holds or names one.
template <typename Resource>
concept ReachesAChannelPole = detail::reaches_a_channel_pole(^^Resource);

// A Resource that a mint of a protocol of its caller admits: a
// SessionResource that reaches no pole of a channel.
template <typename Resource>
concept PlainSessionResource = SessionResource<Resource> && !ReachesAChannelPole<Resource>;

// ── The priority of a session ────────────────────────────────────────
//
// A Resource states the priority of its session in the order of
// fixy/session/Watch.h with a static constexpr member session_priority of
// the type watch::priority.  A Resource that states none has the lowest
// priority.  A member of that name with another type is refused: a plain
// integer with that name could be a count, and the order would read it.

// The priority that a Resource states, or the lowest priority for a
// Resource that states none.  The answer is a function at namespace scope
// that is not a template, so no translation unit can specialize it.
[[nodiscard]] consteval watch::priority session_priority(std::meta::info resource) {
    const std::meta::info member = ::foundation::reflect::member_named(resource, "session_priority");
    if (member == std::meta::info{}) return watch::priority::lowest;
    if (!std::meta::is_variable(member) || !std::meta::is_static_member(member)
        || std::meta::remove_cv(std::meta::type_of(member)) != ^^watch::priority) {
        throw std::meta::exception(
            u8"fixy::session::diagnostic [Session_Priority_Type]: the Resource declares session_priority with a type "
            u8"other than fixy::session::watch::priority, or not as a static member.  Declare it as static constexpr "
            u8"fixy::session::watch::priority session_priority{N};",
            member);
    }
    return std::meta::extract<watch::priority>(member);
}

// The two ends of a channel are one session, so their Resources state one
// priority.
template <typename ResourceA, typename ResourceB>
concept ChannelEndsShareAPriority = session_priority(^^ResourceA) == session_priority(^^ResourceB);

namespace detail {

// True when two loop contexts can hold the two ends of one channel at End.
// The two ends run dual protocols, so the loop around the End of one end is
// the dual of the loop around the End of the other, and none faces none.  A
// permission frame names the set of its own end, and the two ends of a
// channel can hold two sets, so the check reads the Loop of each frame.
[[nodiscard]] consteval bool loops_face_each_other(std::meta::info end_loop_ctx, std::meta::info peer_end_loop_ctx) {
    const std::meta::info loop = loop_of_inner(inner_loop_ctx_of(end_loop_ctx));
    const std::meta::info peer_loop = loop_of_inner(inner_loop_ctx_of(peer_end_loop_ctx));
    if (loop == ^^void || peer_loop == ^^void) return loop == peer_loop;
    return std::meta::dealias(std::meta::substitute(^^dual_of_t, {loop})) == peer_loop;
}

}  // namespace detail

// The two ends of a channel at End stand in loop contexts that face each
// other.  Two handles at End whose loops do not face each other are the ends
// of two channels, and their types show it.
template <typename EndLoopCtx, typename PeerEndLoopCtx>
concept ChannelEndsShareALoop = detail::loops_face_each_other(^^EndLoopCtx, ^^PeerEndLoopCtx);

// The gate of HandleFactory::rewind_channel.  Each end can start at its
// position: R in the loop context LoopCtx, and the peer at the dual of R in
// the dual loop context.  The two Resources share one priority, as the two
// ends of a channel mint do, and the two loops at End face each other.  The
// rewind refuses at run time two ends of two channels that the types do not
// show.
template <typename R, typename LoopCtx, typename EndLoopCtx, typename PeerEndLoopCtx, typename Resource,
          typename PeerResource>
concept RewindableChannelTo =
    RewindableTo<R, LoopCtx, EndLoopCtx> && RewindableTo<dual_of_t<R>, detail::dual_loop_ctx_t<LoopCtx>, PeerEndLoopCtx>
    && ChannelEndsShareAPriority<Resource, PeerResource> && ChannelEndsShareALoop<EndLoopCtx, PeerEndLoopCtx>;

// ── The effect row of a protocol ────────────────────────────────────
//
// A context that starts a session must hold each effect that a message
// of the session carries.  The row of a protocol is the union of the
// rows of its payloads, over each Send and each Recv, on each branch, and
// in each protocol that a DelegatedSession carries.  A received payload
// counts as a sent one does, because the receiver then holds what the
// payload carries.
//
// The walk is the payload walk of fixy/concurrent/PayloadRow.h, with the
// rules of this layer for the families that its rosters do not name:
//
//   - each payload family carries the arguments that its payload rule
//     names in row_arguments (fixy/session/Registry.h).  A marker,
//     DeclassifyOnSend and CTPayload carry their value, a keyed message
//     its payload, and a hand-off the protocol of its endpoint.  A share
//     and a crash record carry no row.
//   - a bare Permission carries no row.
//   - a named state carries the protocol of its name, and not the class of
//     the name.
//   - each other combinator of the registry carries each of its type
//     arguments, and the note of a choice carries none, because its role
//     is a name.
//
// A payload that the walk cannot classify stops the build, and the text
// names it.  The walk stands here and not in
// fixy/session/Classification.h, so a header that reads only the payload
// rules does not include the payload walk.  Complexity: linear in the
// distinct types that the protocol and its payloads reach.
//
// A receive can also give the endpoint a permission: a token, a read loan
// or a returned token of a region.  A touch of that region incurs the row
// of its tag, so the row of the protocol also holds the permission row of
// each region that the protocol delivers to the endpoint
// (protocol_delivered_regions of fixy/session/Flow.h).  A mint checks
// the row of each token that it consumes.  This check covers each token
// that arrives after the mint, in a message or in a delegated endpoint.

namespace detail {

// The rules of this layer, read where a query of `protocol` stands.  The
// walk reads the families of `registry` there, so a family that a header
// registers after this header has its rules.  The rules do not depend on
// the protocol: the parameter only makes the read wait for the query.  The
// registry is a parameter too, because GCC folds a read with constant
// arguments where the body of the function stands.  The walk keeps the
// rules in a stack of fixy/session/Registry.h and reads them through a
// pointer.  It makes the vector one time, at the end.
[[nodiscard]] consteval std::vector<::fixy::concurrent::payload_family_rule>
session_payload_rules(std::meta::info registry, [[maybe_unused]] std::meta::info protocol) {
    using Rule = ::fixy::concurrent::payload_family_rule;
    constexpr std::uint64_t carries_nothing = std::uint64_t{0};
    constexpr std::uint64_t carries_every = ~std::uint64_t{0};
    // A bare Permission is a family that a layer below this one defines, and
    // a named state carries the protocol of its name.
    const Rule named[] = {
        Rule{^^::foundation::permissions::Permission, carries_nothing},
        Rule{^^Named, carries_nothing, ^^named_state_protocol_t},
    };
    stack<Rule> rules{};
    for (const Rule& rule : named)
        push(rules, rule);
    // Each payload family states the arguments that carry its row in its
    // rule, in the family of its own header.
    for (const std::meta::info member :
         ::fixy::session::transition::registrations_of(registry, ^^::fixy::session::transition::payload_rule)) {
        const auto entry = std::meta::extract<::fixy::session::transition::payload_rule>(member);
        push(rules, Rule{entry.shape, entry.row_arguments});
    }
    const auto has_rule = [&rules](std::meta::info family) consteval {
        const Rule* const rule = rules.items;
        for (std::size_t place = 0; place < rules.top; ++place) {
            if (rule[place].family == family) return true;
        }
        return false;
    };
    for (const std::meta::info member :
         ::fixy::session::transition::registrations_of(registry, ^^::fixy::session::transition::combinator)) {
        const auto entry = std::meta::extract<::fixy::session::transition::combinator>(member);
        if (std::meta::is_class_template(entry.shape) && !has_rule(entry.shape)) {
            push(rules, Rule{entry.shape, carries_every});
        }
        if (entry.annotation != std::meta::info{} && !has_rule(entry.annotation)) {
            push(rules, Rule{entry.annotation, carries_nothing});
        }
    }
    return std::vector<Rule>(rules.items, rules.items + rules.top);
}

}  // namespace detail

// The rules of this layer for the payload walk of Proto, read where the
// row of Proto is first asked.  The list has a type that depends on Proto
// (foundation/reflect/Anchor.h), so GCC resolves the static array only
// where the row of a protocol is asked.
template <class Proto>
inline constexpr std::span<const ::fixy::concurrent::payload_family_rule> session_payload_families =
    std::define_static_array(
        ::foundation::reflect::anchored_t<^^Proto, std::vector<::fixy::concurrent::payload_family_rule>>(
            detail::session_payload_rules(detail::protocol_registry, ^^Proto)));

// The union of the effect rows of the payloads of Proto.
template <class Proto>
using protocol_payload_row_t = ::fixy::concurrent::payload_row_under_t<Proto, session_payload_families<Proto>>;

namespace detail {

[[nodiscard]] consteval std::string_view delivered_region_without_row_text(std::meta::info region) {
    std::string text{"fixy::session::diagnostic [Delivered_Region_Without_Row]: a receive of the protocol delivers a "
                     "permission of the region "};
    text += std::meta::display_string_of(region);
    text += ", and its tag declares no permission row.  No program can mint a token for that tag, and the gate "
            "cannot name the effects that a touch of the region incurs.  Declare the row of the tag, as "
            "foundation/permissions/Permission.h states.";
    return std::define_static_string(text);
}

// The union of the permission rows of the regions of the set.  A region
// whose tag declares no row stops the build here, with one diagnostic.
// The union is a function at namespace scope that is not a template, so no
// translation unit can specialize it to give a region a lighter row.
// Complexity: linear in the regions of the set.
[[nodiscard]] consteval std::meta::info permission_rows_of_set(std::meta::info set) {
    std::meta::info row = ^^::foundation::effects::Row<>;
    for (const std::meta::info region : std::meta::template_arguments_of(std::meta::dealias(set))) {
        if (!::foundation::permissions::has_permission_row(region)) {
            throw std::meta::exception(refusal_text(delivered_region_without_row_text(region)), region);
        }
        row = std::meta::dealias(std::meta::substitute(
            ^^::foundation::effects::row_union_t,
            {row, std::meta::substitute(^^::foundation::permissions::permission_row_t, {region})}));
    }
    return row;
}

}  // namespace detail

// The union of the permission rows of the regions that Proto can deliver
// to the endpoint that runs it (protocol_delivered_regions of
// fixy/session/Flow.h).  A delivered tag that declares no row stops
// the build, because no program can mint a token for it.
template <class Proto>
using protocol_delivered_permission_row_t = [:detail::permission_rows_of_set(^^protocol_delivered_regions_t<Proto>):];

// The context admits the effect row of Proto: the row of each payload,
// and the permission row of each region that Proto delivers to the
// endpoint.  Each payload passes the transfer gate first.  A payload that
// the row walk cannot classify stops the build.
template <typename Ctx, typename Proto>
concept CtxAdmitsProtocolRow =
    ::foundation::effects::IsExecCtx<Ctx> && ProtocolPayloadsTransfer<Proto>
    && ::foundation::effects::is_subrow(^^protocol_payload_row_t<Proto>, ^^typename Ctx::row_type)
    && ::foundation::effects::is_subrow(^^protocol_delivered_permission_row_t<Proto>, ^^typename Ctx::row_type);

namespace detail {

// The gate of HandleFactory::recover: the choice is an Offer, branch I of
// it exists and is no label, the permission set is empty, a session can
// start at the branch in the loop context, and the context admits the
// effect row of the branch.
template <typename Ctx, typename Choice, std::size_t I, typename LoopCtx, typename PS>
concept RecoverAdmitted =
    ::foundation::reflect::IsInstanceOf<Choice, ^^Offer> && (I < Choice::branch_count)
    && !::fixy::session::transition::wire_word_of(protocol_registry, ^^Choice, I).is_wired && perm_set_is_empty(^^PS)
    && RewindableTo<std::tuple_element_t<I, typename Choice::branches_tuple>, LoopCtx, LoopCtx>
    && CtxAdmitsProtocolRow<Ctx, std::tuple_element_t<I, typename Choice::branches_tuple>>;

[[nodiscard]] consteval bool recover_admits(std::meta::info ctx, std::meta::info choice, std::size_t index,
                                            std::meta::info loop_ctx, std::meta::info perm_set) {
    return std::meta::extract<bool>(std::meta::substitute(
        ^^RecoverAdmitted, {ctx, choice, std::meta::reflect_constant(index), loop_ctx, perm_set}));
}

}  // namespace detail

// The gate of a session that starts with the tokens Tokens, each a
// Permission<Tag, Brand>.  The set of the first handle holds each token with
// its brand, so the flow closes only when each marker of the protocol names
// the brand of the token that it moves.
template <typename Ctx, typename Proto, typename Resource, typename... Tokens>
concept CtxFitsSessionFrom =
    ::foundation::effects::IsExecCtx<Ctx> && (::foundation::permissions::IsPermission<Tokens> && ...)
    && ::foundation::permissions::DistinctTags<::foundation::permissions::permission_tag_t<Tokens>...>
    && (::foundation::permissions::CtxAdmitsPermission<::foundation::permissions::permission_tag_t<Tokens>, Ctx> && ...)
    && WellFormedRunnableProtocol<Proto> && PlainSessionResource<Resource>
    && PermissionFlowCloses<Proto, ::foundation::permissions::PermSet<Tokens...>> && CtxAdmitsProtocolRow<Ctx, Proto>;

template <typename Ctx, typename Proto, typename Resource, typename... Tokens>
concept CtxFitsPermissionedSession = sizeof...(Tokens) != 0 && CtxFitsSessionFrom<Ctx, Proto, Resource, Tokens...>;

// The row gate of a channel.  Each channel mint gives one context to the
// two sides of the channel.  So the context must admit the row of each
// side, as CtxFitsSessionFrom asks for the one side of a session.  Each
// permission that one side sends arrives at the other side, and the row
// of the other side holds the row of that permission.  Every
// channel mint reads this concept, so the channel mints and the session
// mints cannot drift apart.
template <typename Ctx, typename SelfProto, typename PeerProto>
concept CtxAdmitsChannelRow = CtxAdmitsProtocolRow<Ctx, SelfProto> && CtxAdmitsProtocolRow<Ctx, PeerProto>;

// The context gate of the fork-shaped mint: two runnable local
// protocols, one the dual of the other, a context that admits the row of
// each side, and a fork that the context may start.
template <typename Ctx, typename Proto, typename Parent, typename SelfTag, typename PeerTag>
concept CtxFitsForkedChannel = WellFormedRunnableProtocol<Proto> && WellFormedRunnableProtocol<dual_of_t<Proto>>
                            && PermissionFlowCloses<Proto, ::foundation::permissions::EmptyPermSet>
                            && PermissionFlowCloses<dual_of_t<Proto>, ::foundation::permissions::EmptyPermSet>
                            && CtxAdmitsChannelRow<Ctx, Proto, dual_of_t<Proto>>
                            && ::fixy::concurrent::CtxFitsPermissionFork<Ctx, Parent, SelfTag, PeerTag>;

// The context gate of mint_test_channel of fixy/session/Mint.h, the one
// form that gives the two endpoints to one caller.  It is a hatch for a
// test that drives the two sides on one thread in a fixed order, and the
// gate admits only a context that holds the Test capability.  A
// production context has no Test capability, so it cannot reach that
// mint.  The context must also admit the row of each side, as the context
// of a forked channel must.
template <typename Ctx, typename Proto>
concept CtxFitsTestChannel = ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Test>
                          && WellFormedRunnableProtocol<Proto> && WellFormedRunnableProtocol<dual_of_t<Proto>>
                          && PermissionFlowCloses<Proto, ::foundation::permissions::EmptyPermSet>
                          && PermissionFlowCloses<dual_of_t<Proto>, ::foundation::permissions::EmptyPermSet>
                          && CtxAdmitsChannelRow<Ctx, Proto, dual_of_t<Proto>>;

// ── The relation between the two sides of a channel ─────────────────
//
// The two sides of a channel are exact duals, or a side relation that a
// header registers holds for them.  The door of the mints
// (SessionMintDoor of fixy/session/Handle.h) asks this relation in one
// member, which opens each end of each form.  A side relation is a
// registration of the kind side_relation in a family of the root
// side_relations (fixy/session/Registry.h, section Families).  It names a
// concept `template <class SelfProto, class PeerProto, class ResourceSelf,
// class ResourcePeer> concept`.  The registration stands in the file of
// that concept, which is the header of the mint that admits the relation,
// so the relation comes with the mint that a unit includes.  (GCC 16 gives
// no file for a function template, so the read cannot tie the relation to
// the mint itself.)  The fork door reads the relations where its gate is
// asked, so a unit that does not include an extension admits none of its
// relations.  fixy/session/AsyncChannel.h registers the asynchronous
// relation: each side refines the dual of the other side at the capacity
// that the two Resources state.
//
// The relation names no shape, so the home check of a shape does not hold
// it.  A relation is a declaration of the base, and the rule
// base-namespace refuses a planted one outside the base.
struct side_relation {
    std::meta::info relation{};
};

namespace side_relations {}

namespace detail {

// Has no constant definition.  A call to it stops the constant evaluation,
// and the diagnostic names it.
void a_side_relation_stands_outside_the_file_of_its_concept() noexcept;

// True when a relation of the root `relations` holds for the two sides,
// read where the gate is asked.  The root is a parameter, so GCC does not
// fold the read where this body stands.  Complexity: linear in the
// relations, and one concept check for each.
[[nodiscard]] consteval bool side_relation_holds(std::meta::info relations, std::meta::info self_proto,
                                                 std::meta::info peer_proto, std::meta::info resource_self,
                                                 std::meta::info resource_peer) {
    namespace tr = ::fixy::session::transition;
    for (const std::meta::info member : tr::registrations_of(relations, ^^side_relation)) {
        const side_relation entry = std::meta::extract<side_relation>(member);
        if (!tr::detail::is_same_file(member, entry.relation)) {
            a_side_relation_stands_outside_the_file_of_its_concept();
        }
        if (std::meta::extract<bool>(
                std::meta::substitute(entry.relation, {self_proto, peer_proto, resource_self, resource_peer}))) {
            return true;
        }
    }
    return false;
}

}  // namespace detail

// The relation between the two sides of a channel: exact duals, or a
// registered side relation.
template <typename SelfProto, typename PeerProto, typename ResourceSelf, typename ResourcePeer>
concept ChannelSidesAgree =
    std::is_same_v<PeerProto, dual_of_t<SelfProto>>
    || detail::side_relation_holds(^^side_relations, ^^SelfProto, ^^PeerProto, ^^ResourceSelf, ^^ResourcePeer);

// ── The pole table of a channel ─────────────────────────────────────
//
// A pole table is a registration of the kind pole_table in a family of the
// root pole_tables (fixy/session/Registry.h, section Families).  It names a
// class template `template <class Pole> struct`, constrained to the poles
// that the table knows.  Its specialization for a pole has two member
// types: self_protocol, the protocol that the pole runs, and
// peer_protocol, the protocol that each pole of the other side runs.  So
// the protocol of a channel is one value of its table, and no caller names
// the protocol of one pole.  SessionMintDoor::open_pole asks the relation
// of the two sides of these two protocols in its one member.
//
// The specialization also names the transport of the pole: a static write,
// bool(Ctx const&, Pole&, T&), for a pole that sends, and a static read,
// fixy::Option<T>(Ctx const&, Pole&), for a pole that receives.  Each is
// noexcept and tries one time, and the handle waits through the watch and
// tries again.  A step of a session over the pole takes the context of the
// step and calls the write or the read of the table, so no caller writes the
// transport of a pole, and each session over the pole moves its values in
// one way.  A write or a read that needs a property of the context asks it
// in its own constraint.
//
// The registration stands in the file of its template, which is the header
// of the channel or of the bridge of the channel.  The door reads the
// tables where its gate is asked, so a unit that does not include the
// header of a table opens no pole of its channels.  Two tables that know
// one pole stop the build.  A table is a declaration of the base, and the
// rule base-namespace refuses a planted one outside the base.
struct pole_table {
    std::meta::info table{};
};

namespace pole_tables {}

namespace detail {

// Have no constant definition.  A call to one stops the constant
// evaluation, and the diagnostic names it.
void a_pole_table_stands_outside_the_file_of_its_template() noexcept;
void two_pole_tables_know_one_pole() noexcept;

// The specialization of the one table of the root `tables` that knows the
// pole, or null.  The root is a parameter, so GCC does not fold the read
// where this body stands.  Complexity: linear in the tables.
[[nodiscard]] consteval std::meta::info pole_table_entry(std::meta::info tables, std::meta::info pole) {
    namespace tr = ::fixy::session::transition;
    std::meta::info found{};
    for (const std::meta::info member : tr::registrations_of(tables, ^^pole_table)) {
        const pole_table entry = std::meta::extract<pole_table>(member);
        if (!tr::detail::is_same_file(member, entry.table)) a_pole_table_stands_outside_the_file_of_its_template();
        if (!std::meta::can_substitute(entry.table, {pole})) continue;
        if (found != std::meta::info{}) two_pole_tables_know_one_pole();
        found = std::meta::substitute(entry.table, {pole});
    }
    return found;
}

}  // namespace detail

// A table of the unit knows the pole.
template <typename Pole>
concept HasPoleTable = detail::pole_table_entry(^^pole_tables, ^^Pole) != std::meta::info{};

// The specialization of the table that knows the pole.
template <HasPoleTable Pole>
using pole_table_t = typename[:detail::pole_table_entry(^^pole_tables, ^^Pole):];

// The protocol of the pole, and the protocol of each pole of the other side
// of its channel, from the table that knows the pole.
template <HasPoleTable Pole>
using pole_protocol_t = typename pole_table_t<Pole>::self_protocol;

template <HasPoleTable Pole>
using pole_peer_protocol_t = typename pole_table_t<Pole>::peer_protocol;

// The table of the pole writes a value of type T in the context Ctx.
template <typename Pole, typename Ctx, typename T>
concept PoleWritesIn = ChannelPole<Pole> && HasPoleTable<Pole> && ::foundation::effects::IsExecCtx<Ctx>
                    && requires(Ctx const& ctx, Pole& pole, T& value) {
                           { pole_table_t<Pole>::write(ctx, pole, value) } noexcept -> std::same_as<bool>;
                       };

// The table of the pole reads a value of type T in the context Ctx.  The
// receive of a pole takes the context where a receive of another Resource
// takes its transport, so the context holds no linear value by value
// (HoldsNoLinearValue of foundation/Linear.h) and stores no borrow outside
// the call (StoresNoBorrowOutside of foundation/core/CaptureStore.h), as a
// transport does.  A context holds only its capability and its row, so each
// context passes the store check.
template <typename Pole, typename Ctx, typename T>
concept PoleReadsIn = ChannelPole<Pole> && HasPoleTable<Pole> && ::foundation::effects::IsExecCtx<Ctx>
                   && ::foundation::linear::HoldsNoLinearValue<Ctx> && ::foundation::core::StoresNoBorrowOutside<Ctx>
                   && requires(Ctx const& ctx, Pole& pole) {
                          {
                              pole_table_t<Pole>::read(ctx, pole)
                          } noexcept -> std::same_as<::foundation::core::Option<T>>;
                      };

// The whole gate of SessionMintDoor::open_pole: a pole of a channel held by
// value, which a table knows, two protocols of the table whose sides agree,
// and the gate of a session of one end for the protocol of the pole.
template <typename Ctx, typename Pole>
concept CtxFitsPoleSession = ::foundation::effects::IsExecCtx<Ctx> && ChannelPole<Pole> && SessionResource<Pole>
                          && HasPoleTable<Pole> && WellFormedRunnableProtocol<pole_protocol_t<Pole>>
                          && PermissionFlowCloses<pole_protocol_t<Pole>, ::foundation::permissions::EmptyPermSet>
                          && CtxAdmitsProtocolRow<Ctx, pole_protocol_t<Pole>>
                          && ChannelSidesAgree<pole_protocol_t<Pole>, pole_peer_protocol_t<Pole>, Pole, Pole>;

}  // namespace fixy::session
