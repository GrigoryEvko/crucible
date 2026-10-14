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
#include <fixy/session/ContentAddressed.h>
#include <fixy/session/Flow.h>
#include <fixy/session/Protocol.h>
#include <fixy/session/Registry.h>
#include <fixy/session/Subtype.h>
#include <fixy/session/Watch.h>
#include <fixy/session/Wire.h>

#include <foundation/Pinned.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/PermSet.h>
#include <foundation/permissions/Permission.h>
#include <foundation/reflect/Instance.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
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

// The number of distinct roles that some local types name as peers,
// together.  The walk is collect_named_peers of fixy/session/Flow.h.
// Complexity: quadratic in the number of peer names that the walk finds.
[[nodiscard]] consteval std::size_t distinct_peer_count(std::initializer_list<std::meta::info> protocols) {
    stack<std::meta::info> distinct{};
    for (const std::meta::info protocol : protocols)
        collect_named_peers(protocol, distinct);
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
concept NamesAtMostOnePeer = detail::distinct_peer_count({^^Proto}) <= 1;

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
// Complexity: one visit for each node of the protocol tree.
[[nodiscard]] consteval bool permission_flow_closes(std::meta::info protocol, std::meta::info ps,
                                                    std::meta::info loop_ps) {
    const std::meta::info node = std::meta::dealias(protocol);
    if (node == ^^End) return std::meta::extract<bool>(std::meta::substitute(^^perm_set_admits_close_v, {ps}));
    if (node == ^^Continue) {
        if (std::meta::dealias(loop_ps) == (^^void)) return false;
        return ::foundation::permissions::perm_set_equal(ps, loop_ps);
    }
    if (!std::meta::has_template_arguments(node)) return false;
    const std::meta::info head = std::meta::template_of(node);
    const std::vector<std::meta::info> arguments = std::meta::template_arguments_of(node);
    if (head == ^^Send) {
        if (!std::meta::extract<bool>(std::meta::substitute(^^handle_admits_send_v, {ps, arguments[0]}))) return false;
        return permission_flow_closes(arguments[1], std::meta::substitute(^^perm_set_after_send_t, {ps, arguments[0]}),
                                      loop_ps);
    }
    if (head == ^^Recv) {
        if (!std::meta::extract<bool>(std::meta::substitute(^^handle_admits_recv_v, {ps, arguments[0]}))) return false;
        return permission_flow_closes(arguments[1], std::meta::substitute(^^perm_set_after_recv_t, {ps, arguments[0]}),
                                      loop_ps);
    }
    if (head == ^^Select || head == ^^Offer) {
        const bool is_offer = head == ^^Offer;
        bool is_first = true;
        for (const std::meta::info branch : arguments) {
            // The Sender note of an Offer names a role, and it is no branch.
            const bool is_sender_note = is_first && is_offer && std::meta::has_template_arguments(branch)
                                     && std::meta::template_of(branch) == ^^Sender;
            is_first = false;
            if (!is_sender_note && !permission_flow_closes(branch, ps, loop_ps)) return false;
        }
        return true;
    }
    if (head == ^^Loop) return permission_flow_closes(arguments[0], ps, ps);
    if (head == ^^VendorPinned) return permission_flow_closes(arguments[1], ps, loop_ps);
    return false;
}

}  // namespace detail

// True when every path of Proto, from the set PS, sends only what the set
// holds, receives no second owner of a region, returns each loop to the
// set of its entry, and reaches End with no open loan.
template <typename Proto, typename PS>
concept PermissionFlowCloses = detail::permission_flow_closes(^^Proto, ^^PS, ^^void);

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
                                     && detail::distinct_peer_count({^^R, ^^LoopCtx}) <= 1)));

// The gate of HandleFactory::rewind.
template <typename R, typename LoopCtx, typename EndLoopCtx>
concept RewindableTo = ResumablePosition<R, LoopCtx> && detail::carries_brand<EndLoopCtx, void>;

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
//   - a marker, DeclassifyOnSend and CTPayload carry their value,
//     argument 0.  The token or the loan of a marker is no payload.
//   - PeerMsg and Labelled carry their payload, the last argument.  The
//     peer and the label are names.
//   - a DelegatedSession carries the protocol of its endpoint, argument
//     0.  Its Resource, its policy and its permission set are not
//     messages of that protocol.
//   - a SharedReader, a crash record and a bare Permission carry no row.
//   - each combinator of the registry of fixy/session/Protocol.h carries
//     each of its type arguments, and the note of a choice carries none,
//     because its role is a name.
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

// The walk keeps the rules in a stack of fixy/session/Registry.h
// and reads them through a pointer.  It makes the vector one time, at the end.
[[nodiscard]] consteval std::vector<::fixy::concurrent::payload_family_rule> session_payload_rules() {
    using Rule = ::fixy::concurrent::payload_family_rule;
    constexpr std::uint64_t carries_first = std::uint64_t{1};
    constexpr std::uint64_t carries_nothing = std::uint64_t{0};
    constexpr std::uint64_t carries_every = ~std::uint64_t{0};
    const Rule named[] = {
        Rule{^^Transferable, carries_first},
        Rule{^^Returned, carries_first},
        Rule{^^Borrowed, carries_first},
        Rule{^^Released, carries_first},
        Rule{^^DeclassifyOnSend, carries_first},
        Rule{^^CTPayload, carries_first},
        Rule{^^PeerMsg, std::uint64_t{1} << 2},
        Rule{^^Labelled, std::uint64_t{1} << 1},
        Rule{^^ContentAddressed, carries_first},
        Rule{^^DelegatedSession, carries_first},
        Rule{^^SharedReader, carries_nothing},
        Rule{^^Crash, carries_nothing},
        Rule{^^::foundation::permissions::Permission, carries_nothing},
    };
    stack<Rule> rules{};
    for (const Rule& rule : named)
        push(rules, rule);
    const auto has_rule = [&rules](std::meta::info family) consteval {
        const Rule* const rule = rules.items;
        for (std::size_t place = 0; place < rules.top; ++place) {
            if (rule[place].family == family) return true;
        }
        return false;
    };
    const std::vector<std::meta::info> members =
        std::meta::members_of(protocol_registry, std::meta::access_context::current());
    const std::meta::info* const member = members.data();
    const std::size_t member_count = members.size();
    for (std::size_t place = 0; place < member_count; ++place) {
        if (!std::meta::is_variable(member[place])) continue;
        if (std::meta::remove_cvref(std::meta::type_of(member[place])) != ^^::fixy::session::transition::combinator)
            continue;
        const auto entry = std::meta::extract<::fixy::session::transition::combinator>(member[place]);
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

// The rules of this layer for the payload walk.
inline constexpr std::span<const ::fixy::concurrent::payload_family_rule> session_payload_families =
    std::define_static_array(detail::session_payload_rules());

// The union of the effect rows of the payloads of Proto.
template <class Proto>
using protocol_payload_row_t = ::fixy::concurrent::payload_row_under_t<Proto, session_payload_families>;

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
// endpoint.  A payload that the walk cannot classify stops the build.
template <typename Ctx, typename Proto>
concept CtxAdmitsProtocolRow =
    ::foundation::effects::IsExecCtx<Ctx>
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

template <typename Ctx, typename Proto, typename Resource, typename... Tags>
concept CtxFitsSessionFrom =
    ::foundation::effects::IsExecCtx<Ctx> && ::foundation::permissions::DistinctTags<Tags...>
    && (::foundation::permissions::CtxAdmitsPermission<Tags, Ctx> && ...)
    && WellFormedRunnableProtocol<Proto> && SessionResource<Resource>
    && PermissionFlowCloses<Proto, ::foundation::permissions::PermSet<Tags...>> && CtxAdmitsProtocolRow<Ctx, Proto>;

template <typename Ctx, typename Proto, typename Resource, typename... Tags>
concept CtxFitsPermissionedSession = sizeof...(Tags) != 0 && CtxFitsSessionFrom<Ctx, Proto, Resource, Tags...>;

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

// The relation between the two sides of a forked channel.  The sides are
// exact duals, or each side refines the dual of the other side at the
// capacity that the two Resources state (fixy/session/Subtype.h).  The
// relation asks for both directions because refinement keeps each exit,
// and duality does not keep that property.  mint_forked_async_channel of
// fixy/session/AsyncChannel.h states the second form with a diagnostic
// for each clause.
template <typename SelfProto, typename PeerProto, typename ResourceSelf, typename ResourcePeer>
concept ForkedSidesAgree = std::is_same_v<PeerProto, dual_of_t<SelfProto>>
                        || (StatesOneChannelCapacity<ResourceSelf, ResourcePeer>
                            && SubtypeAsync<SelfProto, dual_of_t<PeerProto>, ResourceSelf>
                            && SubtypeAsync<PeerProto, dual_of_t<SelfProto>, ResourcePeer>);

}  // namespace fixy::session
