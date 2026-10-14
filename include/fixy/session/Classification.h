#pragma once

// The walk that classifies a session payload.
//
// The walk reads each component of a payload.  It gives the facts of the
// payload and the gates that read them.  fixy/session/Flow.h gives what a
// message does to the permission sets of its two endpoints.
//
// ── How the header finds the tokens ─────────────────────────────────
//
// The walk reads every component of the payload, as
// foundation/reflect/TypeComponents.h defines a component.  A walk that
// read the marker only at the top of the payload would send
// std::pair<Transferable<int, X>, int> with an empty set, and the sender
// would keep X in its set while the recipient held the token.  The walk
// also knows how each component was reached:
//
//   owned      the root, a base, a by-value member of an owned class, and
//              the value that a marker carries
//   aliased    through a pointer, a reference, a reference member, or a
//              root that is itself a reference type
//   in union   a member of a union, which covers std::optional and
//              std::variant, whose storage is a union
//   in array   an element of an array
//   named      a template argument.  The value holds no object of it, so
//              the walk reads it for delegation only
//
// A token counts only when the walk reaches it owned.  A token reached
// aliased is a second name for a token that stays with the sender.  A
// token in a union can be absent at run time.  A token in an array is
// several tokens of one tag, which a set cannot hold.  The walk refuses
// each of these, and it refuses these shapes too:
//
//   * a read proof, a share or a pool outside its marker
//   * a session endpoint that the payload holds, points at or names, and
//     a hand-off that it does not hold by value (the section on
//     delegation below)
//   * a type-erasure family: std::function, std::move_only_function,
//     std::copyable_function, std::function_ref and std::any, because
//     the static type does not name what they hold
//   * a class whose state the walk cannot read, which is the shape of a
//     lambda with captures
//   * a class or a template that is only declared, behind a pointer, a
//     reference or a template argument
//   * one tag twice in one payload
//   * a fixy::Secret outside DeclassifyOnSend, and a type marked
//     constant_time_value outside CTPayload, as fixy/session/Classified.h
//     states.
//
// A refusal is a compile error.  Its text names the refused type.
//
// The walk reads each class behind a pointer or a reference for its
// members, and it instantiates a specialization for that read.  So a
// token in a member that no template argument names is seen too, and
// std::unique_ptr<Box<int>> cannot launder the token that Box holds.
// TypeComponents.h states the costs of that read.  A class that is only
// declared is complete in a different unit, so the verdict on it is a
// compile error of every trait here and never a value.
//
// One walk gives every answer of this header for a payload: the four
// sets, the refusal, the delegation and the hand-offs.  A sealed cache
// (the section of that name below) holds the result, so a translation
// unit walks each payload one time, and no user specialization changes
// a verdict.
//
// The walk enters each type one time for each reach.  A type that it
// reaches owned a second time is a second copy of the tokens it holds.
// The walk refuses that copy as one tag twice, and it does not read the
// type again.  So a struct of 1,024 members of one type reads that type
// one time, and a binary tree of by-value members reads each level one
// time.
//
// ── Delegation ──────────────────────────────────────────────────────
//
// A payload delegates when it gives the recipient authority over a
// session endpoint.  One form of delegation travels: the hand-off
// DelegatedSession<P, R, Pol, PS>, held by value.  Its tags move with it,
// and fixy/session/Delegate.h states the rules that it obeys.  In every
// session the walk refuses an endpoint that a type names outside that
// hand-off.  An endpoint that travels bare carries its permission set with
// no set change, and it skips the rules of the hand-off: the handle
// outside every Loop, with no brand, and no hand-off to a peer of its own
// session.  A hand-off that a template argument names holds no endpoint,
// and it changes no set.  The walk also refuses the carriers whose content
// it cannot read: a type-erasure family and a lambda with captures.
//
// A pointer to void and a pointer to a function name no endpoint type.
// They are in the class of an integer that holds an address, which the
// list below states no type can refuse.  A plain session admits them, as
// data.  Crash sessions and checkpoint sessions refuse them, and the
// hand-off too, because their theories have no delegation.  Each of them
// reads payload_conveys_delegation_v, so the two sessions use one
// definition of delegation.
//
// The query reads every component of the payload, and every reach
// counts.  A pointer or a reference to an endpoint counts too.  The
// recipient can step the endpoint through it, and it is then a second
// name for the endpoint.  A template argument counts as well, because a
// type that names an endpoint can hold one.  A component delegates when
// it is one of these:
//
//   the hand-off       DelegatedSession<P, R, Pol, PS>
//   an endpoint        a class that declares a member type named protocol
//                      or protocol_type, and that is not a protocol
//                      itself.  Each handle, decorator and bridge of this
//                      tree has that shape: SessionHandle and its base,
//                      CrashWatched, CheckpointHandle, Recorded and
//                      SessionFromMachine.  The handle of a library that
//                      fixy does not know has it too
//
// The walk reads the members of each class it reaches.  A specialization
// that the payload points at or names is instantiated for the read, so a
// specialization that cannot be instantiated here stops the build with
// the diagnostic of its own template.
//
// The query also counts a component with content that it cannot read.
// Each of these can hold an endpoint that the static type does not name:
// a type-erasure family, a class with state that the walk cannot read (a
// lambda with captures), and a pointer to void.  A class or a template
// that is only declared stops the build.  The permission verdicts stop it
// with [Payload_Refused], and the delegation query with
// [Payload_Delegation_Unreadable].  A value would say different things in
// a unit that defines the class and in a unit that only declares it.
//
// A pointer or a reference to a function, and a pointer to a member
// function, delegate too.  The recipient runs the target, and the target
// can step an endpoint that the sender put in state the target reaches.
// The query cannot read the target, so it counts the pointer.
//
// What the query cannot see:
//
//   * an integer that holds the address of an endpoint, or an index
//     into a table of endpoints.  An integer has no type provenance, and
//     the query reads types.  std::uintptr_t is also the type of each
//     plain 64-bit count, so no type can name the address form
//   * a copy of the Resource of a live session.  A Resource of raw
//     pointers is a channel held as plain data, and no type marks it.
//
// The crash attack campaign pins each of these on its ledger.

#include <foundation/Platform.h>
#include <foundation/permissions/PermSet.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>
#include <foundation/permissions/SharedPermission.h>
#include <foundation/reflect/Anchor.h>
#include <foundation/reflect/TypeComponents.h>
#include <fixy/session/Classified.h>
#include <fixy/session/Fold.h>
#include <fixy/session/Protocol.h>
#include <fixy/session/Registry.h>

#include <any>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <meta>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace fixy::session {

// ── Loan states in a permission set ─────────────────────────────────
//
// Each is an empty tag, so a PermSet holds it as it holds any other tag.
// The region of LentOut<Tag> and of BorrowedIn<Tag> is Tag.

template <class Tag>
struct LentOut {};

template <class Tag>
struct BorrowedIn {};

// The markers that fixy/session/Flow.h defines.  The walk reads a marker
// by its family and by its template arguments, so no definition is
// necessary here.
template <class T, class Tag>
struct Transferable;
template <class T, class Tag>
struct Returned;
template <class T, class Tag>
struct Borrowed;
template <class T, class Tag>
struct Released;
template <class Tag, class Brand>
class SharedReader;

// The first component of a payload that gives authority over a session
// endpoint, as the head of this header states it.  None means that the
// payload delegates nothing.  A class that the query cannot read is a
// compile error and has no value here.
enum class DelegationCarrier : std::uint8_t {
    None,
    HandOff,
    Endpoint,
    TypeErasure,
    UnreadableState,
    OpaquePointer,
    FunctionPointer,
};

namespace detail {

// A walk of this header changes no vector in a loop.  Each list that a walk
// makes is a stack of fixy/session/Registry.h, and the walk reads it
// through a pointer.  That header states the costs of a vector in a
// constant evaluation of this GCC 16 build.
using ::fixy::session::transition::make_room;
using ::fixy::session::transition::push;
using ::fixy::session::transition::stack;

// ── Why a payload is refused ────────────────────────────────────────

enum class PayloadRefusal : std::uint8_t {
    None,
    TokenBehindPointer,
    TokenInUnion,
    TokenInArray,
    DuplicateTag,
    BareBorrowOrShare,
    TypeErasure,
    UnreadableState,
    IncompleteType,
    ClassifiedBare,
    ConstantTimeBare,
    BareEndpoint,
    HandOffNotOwned,
};

enum class PayloadReach : std::uint8_t {
    Owned,
    Aliased,
    InUnion,
    InArray,
    Named,
};

// The families the walk classifies by name.  Each entry reflects a class
// template, so one entry covers each specialization of it.

// A read proof, a read loan, a parked token, a share or a pool.  Each
// travels only inside a marker.
inline constexpr std::meta::info payload_proof_families[] = {
    ^^::foundation::permissions::ReadView,
    ^^::foundation::permissions::ReadLoan,
    ^^::foundation::permissions::LentPermission,
    ^^::foundation::permissions::SharedPermission,
    ^^::foundation::permissions::SharedPermissionGuard,
    ^^::foundation::permissions::SharedPermissionPool,
};

// A type whose static type does not name what it holds.
inline constexpr std::meta::info payload_type_erasure_families[] = {
    ^^std::function,
    ^^std::move_only_function,
    ^^std::copyable_function,
    ^^std::function_ref,
};

[[nodiscard]] consteval bool payload_is_specialization(std::meta::info type) noexcept {
    return std::meta::has_template_arguments(std::meta::dealias(type));
}

[[nodiscard]] consteval bool payload_family_is(std::meta::info type, std::meta::info family) noexcept {
    return payload_is_specialization(type) && std::meta::template_of(std::meta::dealias(type)) == family;
}

[[nodiscard]] consteval bool payload_family_is_on(std::meta::info type,
                                                  std::span<const std::meta::info> roster) noexcept {
    for (const std::meta::info family : roster) {
        if (payload_family_is(type, family)) return true;
    }
    return false;
}

// True when one of the `count` types at `types` is `type`.  Complexity:
// linear in `count`.
[[nodiscard]] consteval bool holds_type(const std::meta::info* types, std::size_t count,
                                        std::meta::info type) noexcept {
    for (std::size_t place = 0; place < count; ++place) {
        if (types[place] == type) return true;
    }
    return false;
}

[[nodiscard]] consteval bool holds_type(const std::vector<std::meta::info>& types, std::meta::info type) {
    return holds_type(types.data(), types.size(), type);
}

[[nodiscard]] consteval bool holds_type(const stack<std::meta::info>& types, std::meta::info type) {
    return holds_type(types.items, types.top, type);
}

// ── What delegates ──────────────────────────────────────────────────

// True when the type is a session protocol: its head is a combinator
// that fixy/session/Protocol.h, or a header that extends it, registers.
[[nodiscard]] consteval bool is_session_protocol_type(std::meta::info type) {
    return std::meta::is_type(type) && ::fixy::session::transition::is_registered(protocol_registry, type);
}

// True when the class declares a member type named protocol or
// protocol_type, which is how an endpoint names the protocol it runs.
// Complexity: linear in the number of members.
[[nodiscard]] consteval bool has_protocol_member(std::meta::info type) {
    for (const std::meta::info member : std::meta::members_of(type, std::meta::access_context::unchecked())) {
        if (!std::meta::is_type(member) || !std::meta::has_identifier(member)) continue;
        const std::string_view name = std::meta::identifier_of(member);
        if (name == "protocol" || name == "protocol_type") return true;
    }
    return false;
}

// What one component of a payload carries, or no value for a class that
// the walk cannot read.  The walk instantiates each specialization it
// reaches, so such a class has no definition here.
[[nodiscard]] consteval std::optional<DelegationCarrier> delegation_carrier_of(::foundation::reflect::TypeNode node) {
    const std::meta::info type = node.type;
    if (type == std::meta::info{}) return DelegationCarrier::None;
    if (payload_family_is(type, ^^DelegatedSession)) return DelegationCarrier::HandOff;
    if (payload_family_is_on(type, payload_type_erasure_families) || type == ^^std::any) {
        return DelegationCarrier::TypeErasure;
    }
    if (std::meta::is_pointer_type(type)
        && std::meta::is_void_type(std::meta::remove_cv(std::meta::remove_pointer(type)))) {
        return DelegationCarrier::OpaquePointer;
    }
    // The walk reaches the function type through a pointer or a reference
    // to it.  A pointer to a member function has no component below it,
    // so the query reads it here.
    if (std::meta::is_function_type(type) || std::meta::is_member_function_pointer_type(type)) {
        return DelegationCarrier::FunctionPointer;
    }
    if (!std::meta::is_class_type(type) && !std::meta::is_union_type(type)) return DelegationCarrier::None;
    if (!node.may_read_members) return std::nullopt;
    if (::foundation::reflect::holds_unreadable_state(node)) return DelegationCarrier::UnreadableState;
    // A protocol is a type that names a conversation.  It is not an
    // endpoint of one, although VendorPinned declares a member protocol.
    if (has_protocol_member(type) && !is_session_protocol_type(type)) return DelegationCarrier::Endpoint;
    return DelegationCarrier::None;
}

// ── The walk ────────────────────────────────────────────────────────

// The families that the walk reads by name.  The walk does not read the
// members of such a family, so a family that another header defines needs
// no definition here.  None of them is an endpoint.
enum class PayloadFamily : std::uint8_t {
    Other,
    Token,  // Permission<Tag>
    Moves,  // Transferable<T, Tag>, Returned<T, Tag>
    Lends,  // Borrowed<T, Tag>
    Releases,  // Released<T, Tag>
    Share,  // SharedReader<Tag, Brand>
    Keyed,  // PeerMsg<Q, L, U>, Labelled<L, U>
    Declassified,  // DeclassifyOnSend<T, Policy>
    ConstantTime,  // CTPayload<T>
    Classified,  // fixy::Secret<T>
    Proof,  // a family of payload_proof_families
};

[[nodiscard]] consteval PayloadFamily payload_family_of(std::meta::info type) {
    if (!payload_is_specialization(type)) return PayloadFamily::Other;
    const std::meta::info family = std::meta::template_of(std::meta::dealias(type));
    if (family == ^^::foundation::permissions::Permission) return PayloadFamily::Token;
    if (family == ^^Transferable || family == ^^Returned) return PayloadFamily::Moves;
    if (family == ^^Borrowed) return PayloadFamily::Lends;
    if (family == ^^Released) return PayloadFamily::Releases;
    if (family == ^^SharedReader) return PayloadFamily::Share;
    if (family == ^^PeerMsg || family == ^^Labelled) return PayloadFamily::Keyed;
    if (family == ^^DeclassifyOnSend) return PayloadFamily::Declassified;
    if (family == ^^CTPayload) return PayloadFamily::ConstantTime;
    if (family == ^^::fixy::Secret) return PayloadFamily::Classified;
    if (payload_family_is_on(type, payload_proof_families)) return PayloadFamily::Proof;
    return PayloadFamily::Other;
}

// One component: its type, a flag that says if the walk can read its
// members, how the walk reached it, and a flag set on the value that a
// CTPayload carries and on its parts, the one place where a constant-time
// value can travel.
struct PayloadNode {
    std::meta::info type{};
    bool readable = false;
    PayloadReach reach = PayloadReach::Owned;
    bool in_ct_carrier = false;
};

[[nodiscard]] consteval bool is_same_payload_node(const PayloadNode& lhs, const PayloadNode& rhs) noexcept {
    return lhs.type == rhs.type && lhs.readable == rhs.readable && lhs.reach == rhs.reach
        && lhs.in_ct_carrier == rhs.in_ct_carrier;
}

// What the walk found in one payload.  This form holds stacks, so it
// lives only inside a constant evaluation.  payload_facts below keeps the
// results in a form that a static data member can hold.
struct PayloadCensus {
    // The tags that the payload moves, lends and releases.
    stack<std::meta::info> moved;
    stack<std::meta::info> lent;
    stack<std::meta::info> released;
    // Each DelegatedSession that the payload holds by value.
    stack<std::meta::info> hand_offs;
    bool carries_share = false;
    PayloadRefusal refusal = PayloadRefusal::None;
    std::meta::info refused_type{};
    // The first component that delegates or that the walk cannot read.
    bool has_carrier = false;
    DelegationCarrier carrier = DelegationCarrier::None;
    bool is_carrier_readable = true;
    std::meta::info carrier_type{};
};

// A node that the walk entered, and the first token that it found owned
// below that node, or null.
struct PayloadVisit {
    PayloadNode node{};
    std::meta::info first_token{};
};

// `parts` holds one run of parts for each class that the walk is inside.
// The run of a class is above the run of the class that holds it, and the
// walk takes the run off when it leaves the class.
struct PayloadWalk {
    PayloadCensus census;
    stack<PayloadVisit> visits;
    stack<PayloadNode> parts;
};

// The walk stops when it has a refusal and knows the first carrier.
[[nodiscard]] consteval bool is_walk_settled(const PayloadWalk& walk) noexcept {
    return walk.census.refusal != PayloadRefusal::None && walk.census.has_carrier;
}

consteval void refuse_payload(PayloadWalk& walk, PayloadRefusal why, std::meta::info type) noexcept {
    if (walk.census.refusal != PayloadRefusal::None) return;
    walk.census.refusal = why;
    walk.census.refused_type = type;
}

consteval void note_carrier(PayloadWalk& walk, std::optional<DelegationCarrier> carrier,
                            std::meta::info type) noexcept {
    if (walk.census.has_carrier) return;
    if (carrier.has_value() && *carrier == DelegationCarrier::None) return;
    walk.census.has_carrier = true;
    walk.census.is_carrier_readable = carrier.has_value();
    walk.census.carrier = carrier.value_or(DelegationCarrier::None);
    walk.census.carrier_type = type;
}

// Records one tag that the payload moves, lends or releases, and refuses
// a tag that the payload names already.  A refused payload changes no
// set, so the walk records nothing after a refusal.
consteval void record_payload_tag(PayloadWalk& walk, stack<std::meta::info> PayloadCensus::* into, std::meta::info tag,
                                  std::meta::info at) {
    if (walk.census.refusal != PayloadRefusal::None) return;
    const std::meta::info bare_tag = std::meta::dealias(tag);
    for (const auto held : {&PayloadCensus::moved, &PayloadCensus::lent, &PayloadCensus::released}) {
        if (holds_type(walk.census.*held, bare_tag)) {
            refuse_payload(walk, PayloadRefusal::DuplicateTag, at);
            return;
        }
    }
    push(walk.census.*into, bare_tag);
}

[[nodiscard]] consteval PayloadRefusal payload_refusal_for_reach(PayloadReach reach) noexcept {
    switch (reach) {
        case PayloadReach::Aliased:
            return PayloadRefusal::TokenBehindPointer;
        case PayloadReach::InUnion:
            return PayloadRefusal::TokenInUnion;
        case PayloadReach::InArray:
            return PayloadRefusal::TokenInArray;
        case PayloadReach::Owned:
        case PayloadReach::Named:
            return PayloadRefusal::None;
        default:
            break;
    }
    return PayloadRefusal::None;
}

[[nodiscard]] consteval PayloadRefusal payload_refusal_for_carrier(DelegationCarrier carrier) noexcept {
    switch (carrier) {
        case DelegationCarrier::Endpoint:
            return PayloadRefusal::BareEndpoint;
        case DelegationCarrier::TypeErasure:
            return PayloadRefusal::TypeErasure;
        case DelegationCarrier::UnreadableState:
            return PayloadRefusal::UnreadableState;
        // An opaque pointer and a function pointer name no endpoint type.
        // They are in the class of an integer that holds an address, which
        // no type can refuse, so a plain session admits them.  A crash and a
        // checkpoint session refuse them through payload_conveys_delegation_v.
        case DelegationCarrier::OpaquePointer:
        case DelegationCarrier::FunctionPointer:
        case DelegationCarrier::None:
        case DelegationCarrier::HandOff:
            return PayloadRefusal::None;
        default:
            break;
    }
    return PayloadRefusal::None;
}

// A marker or a token counts only when the walk reaches it owned.
[[nodiscard]] consteval bool is_owned_or_refused(PayloadWalk& walk, const PayloadNode& node) noexcept {
    if (node.reach == PayloadReach::Owned) return true;
    refuse_payload(walk, payload_refusal_for_reach(node.reach), node.type);
    return false;
}

consteval std::meta::info visit_payload_node(PayloadWalk& walk, PayloadNode node);

// A hand-off moves the tags of the set of its endpoint.  A hand-off that
// a template argument names holds no endpoint.  A hand-off behind a
// pointer, in a union or in an array is refused: it travels by value, one
// time, so that one recipient holds the endpoint and its tokens.  The
// first token of a hand-off is the hand-off, when its set holds a tag.
// `argument` points to the template arguments of the hand-off.
[[nodiscard]] consteval std::meta::info take_hand_off(PayloadWalk& walk, const PayloadNode& node,
                                                      const std::meta::info* argument) {
    if (node.reach == PayloadReach::Named) return {};
    if (node.reach != PayloadReach::Owned) {
        refuse_payload(walk, PayloadRefusal::HandOffNotOwned, node.type);
        return {};
    }
    const std::meta::info inner_set = std::meta::dealias(argument[3]);
    const std::vector<std::meta::info> inner_tags = std::meta::template_arguments_of(inner_set);
    const std::meta::info* const tag = inner_tags.data();
    const std::size_t tag_count = inner_tags.size();
    for (std::size_t place = 0; place < tag_count; ++place)
        record_payload_tag(walk, &PayloadCensus::moved, tag[place], node.type);
    if (walk.census.refusal == PayloadRefusal::None) push(walk.census.hand_offs, node.type);
    return tag_count == 0 ? std::meta::info{} : node.type;
}

// A marker records its tag, and the value that it carries is a by-value
// member of the marker.  `argument` points to the template arguments of
// the marker.
[[nodiscard]] consteval std::meta::info take_marker(PayloadWalk& walk, const PayloadNode& node,
                                                    const std::meta::info* argument,
                                                    stack<std::meta::info> PayloadCensus::* into) {
    if (!is_owned_or_refused(walk, node)) return {};
    record_payload_tag(walk, into, argument[1], node.type);
    static_cast<void>(visit_payload_node(
        walk, PayloadNode{::foundation::reflect::bare_type(argument[0]), true, PayloadReach::Owned}));
    return node.type;
}

// Puts the nodes one step below a class on `parts`: each base and each
// non-static data member, in declaration order.  Returns their count.
[[nodiscard]] consteval std::size_t push_payload_parts(stack<PayloadNode>& parts, const PayloadNode& node) {
    namespace refl = ::foundation::reflect;
    const bool is_union = std::meta::is_union_type(node.type);
    const PayloadReach part_reach = is_union && node.reach == PayloadReach::Owned ? PayloadReach::InUnion : node.reach;
    const PayloadReach alias_reach = node.reach == PayloadReach::Named ? PayloadReach::Named : PayloadReach::Aliased;
    const auto unchecked = std::meta::access_context::unchecked();
    const std::size_t first = parts.top;
    const std::vector<std::meta::info> bases = std::meta::bases_of(node.type, unchecked);
    const std::meta::info* const base = bases.data();
    const std::size_t base_count = bases.size();
    make_room(parts, base_count);
    for (std::size_t place = 0; place < base_count; ++place) {
        parts.items[parts.top++] =
            PayloadNode{refl::bare_type(std::meta::type_of(base[place])), true, part_reach, node.in_ct_carrier};
    }
    const std::vector<std::meta::info> members = std::meta::nonstatic_data_members_of(node.type, unchecked);
    const std::meta::info* const member = members.data();
    const std::size_t member_count = members.size();
    make_room(parts, member_count);
    for (std::size_t place = 0; place < member_count; ++place) {
        const std::meta::info member_type = std::meta::type_of(member[place]);
        if (std::meta::is_reference_type(member_type)) {
            const refl::TypeNode reached =
                refl::node_reached_indirectly(member_type, refl::SpecializationRead::Instantiating);
            parts.items[parts.top++] = PayloadNode{reached.type, reached.may_read_members, alias_reach};
        } else {
            parts.items[parts.top++] = PayloadNode{refl::bare_type(member_type), true, part_reach, node.in_ct_carrier};
        }
    }
    return parts.top - first;
}

// The argument that names the value a family carries, or no argument.
// A token, a share and a proof carry no value that can hold an endpoint.
[[nodiscard]] consteval std::optional<std::size_t> carried_value_argument(PayloadFamily family,
                                                                          std::size_t argument_count) noexcept {
    switch (family) {
        case PayloadFamily::Moves:
        case PayloadFamily::Lends:
        case PayloadFamily::Releases:
        case PayloadFamily::Declassified:
        case PayloadFamily::ConstantTime:
        case PayloadFamily::Classified:
            return 0;
        case PayloadFamily::Keyed:
            return argument_count - 1;
        case PayloadFamily::Other:
        case PayloadFamily::Token:
        case PayloadFamily::Share:
        case PayloadFamily::Proof:
            return std::nullopt;
        default:
            break;
    }
    return std::nullopt;
}

// Reads one template argument as a type that the value names, for
// delegation only.  The value holds no object of it.
consteval void visit_named_argument(PayloadWalk& walk, std::meta::info argument) {
    namespace refl = ::foundation::reflect;
    std::meta::info argument_type{};
    if (std::meta::is_type(argument)) {
        argument_type = argument;
    } else if (std::meta::is_value(argument) || std::meta::is_object(argument)) {
        argument_type = std::meta::type_of(argument);
    } else {
        return;
    }
    const refl::TypeNode reached =
        refl::node_reached_indirectly(argument_type, refl::SpecializationRead::Instantiating);
    static_cast<void>(
        visit_payload_node(walk, PayloadNode{reached.type, reached.may_read_members, PayloadReach::Named}));
}

// Reads one node: its delegation, its template arguments, its family,
// and the nodes below it.  The walk reads the arguments and the parts
// from the last to the first, which is the order of the component walk of
// foundation/reflect/TypeComponents.h.  It returns the first token that
// the node holds owned, or null.
[[nodiscard]] consteval std::meta::info enter_payload_node(PayloadWalk& walk, const PayloadNode& node) {
    namespace refl = ::foundation::reflect;
    constexpr refl::SpecializationRead instantiating = refl::SpecializationRead::Instantiating;
    const std::meta::info type = node.type;
    const PayloadReach reach = node.reach;
    const PayloadFamily family = payload_family_of(type);
    std::vector<std::meta::info> arguments;
    if (payload_is_specialization(type)) arguments = std::meta::template_arguments_of(type);
    const std::meta::info* const argument = arguments.data();
    const std::size_t argument_count = arguments.size();

    if (family != PayloadFamily::Other) {
        // A family that the walk reads by name is no endpoint.  A named one
        // is read for the value that it carries, and for nothing else.
        const std::optional<std::size_t> carried = carried_value_argument(family, argument_count);
        if (reach == PayloadReach::Named) {
            if (carried.has_value()) visit_named_argument(walk, argument[*carried]);
            return {};
        }
    } else {
        // Each other component says what it delegates, at every reach.
        const std::optional<DelegationCarrier> carrier = delegation_carrier_of(refl::TypeNode{type, node.readable});
        note_carrier(walk, carrier, type);
        if (!carrier.has_value()) {
            refuse_payload(walk, PayloadRefusal::IncompleteType, type);
            return {};
        }
        if (*carrier == DelegationCarrier::HandOff) return take_hand_off(walk, node, argument);
        if (*carrier != DelegationCarrier::None) {
            const PayloadRefusal refusal = payload_refusal_for_carrier(*carrier);
            if (refusal != PayloadRefusal::None) refuse_payload(walk, refusal, type);
            return {};
        }
        for (std::size_t index = argument_count; index-- > 0;) {
            visit_named_argument(walk, argument[index]);
            if (is_walk_settled(walk)) return {};
        }
    }

    switch (family) {
        case PayloadFamily::Token:
            if (!is_owned_or_refused(walk, node)) return {};
            record_payload_tag(walk, &PayloadCensus::moved, argument[0], type);
            return type;
        case PayloadFamily::Moves:
            return take_marker(walk, node, argument, &PayloadCensus::moved);
        case PayloadFamily::Lends:
            return take_marker(walk, node, argument, &PayloadCensus::lent);
        case PayloadFamily::Releases:
            return take_marker(walk, node, argument, &PayloadCensus::released);
        case PayloadFamily::Share:
            if (is_owned_or_refused(walk, node)) walk.census.carries_share = true;
            return {};
        // A keyed message is its label word and then the value of its
        // payload, which the value step of the handle moves.  The payload
        // counts as a member of the message.  A payload of void is no
        // class, so it adds nothing.
        case PayloadFamily::Keyed:
            return visit_payload_node(
                walk, PayloadNode{refl::bare_type(argument[argument_count - 1]), true, reach, node.in_ct_carrier});
        // The carrier declassifies at the transport, so the Secret it holds
        // is not read.  The value it will hand over is, for the tokens in
        // it.
        case PayloadFamily::Declassified:
            return visit_payload_node(walk, PayloadNode{refl::bare_type(argument[0]), true, reach});
        case PayloadFamily::ConstantTime:
            return visit_payload_node(walk, PayloadNode{refl::bare_type(argument[0]), true, reach, true});
        // The walk still reads the classified value for delegation, so the
        // carrier of the payload stays exact.
        case PayloadFamily::Classified:
            refuse_payload(walk, PayloadRefusal::ClassifiedBare, type);
            visit_named_argument(walk, argument[0]);
            return {};
        case PayloadFamily::Proof:
            refuse_payload(walk, PayloadRefusal::BareBorrowOrShare, type);
            return {};
        case PayloadFamily::Other:
            break;
        default:
            break;
    }
    if (reach != PayloadReach::Named && !node.in_ct_carrier && carries_constant_time_mark(type)) {
        refuse_payload(walk, PayloadRefusal::ConstantTimeBare, type);
        return {};
    }

    // A pointer to a data member names its class and its member type.  The
    // value holds an offset and no object of either, so the walk reads each
    // for delegation only.  A pointer to a member function is a carrier,
    // and the walk stopped on it above.
    if (std::meta::is_member_pointer_type(type)) {
        visit_named_argument(walk, refl::member_pointer_member_of(type));
        visit_named_argument(walk, refl::member_pointer_class_of(type));
        return {};
    }
    if (std::meta::is_pointer_type(type) || std::meta::is_reference_type(type)) {
        const std::meta::info element =
            std::meta::is_pointer_type(type) ? std::meta::remove_pointer(type) : std::meta::remove_reference(type);
        const refl::TypeNode reached = refl::node_reached_indirectly(element, instantiating);
        const PayloadReach element_reach = reach == PayloadReach::Named ? PayloadReach::Named : PayloadReach::Aliased;
        static_cast<void>(visit_payload_node(walk, PayloadNode{reached.type, reached.may_read_members, element_reach}));
        return {};
    }
    if (std::meta::is_array_type(type)) {
        const PayloadReach element_reach = reach == PayloadReach::Owned ? PayloadReach::InArray : reach;
        static_cast<void>(visit_payload_node(walk, PayloadNode{refl::bare_type(std::meta::remove_all_extents(type)),
                                                               node.readable, element_reach, node.in_ct_carrier}));
        return {};
    }
    if (!std::meta::is_class_type(type) && !std::meta::is_union_type(type)) return {};

    // A visit can make the storage of the parts larger, so the walk reads
    // each part through the stack, and not through a pointer that it keeps.
    const std::size_t first = walk.parts.top;
    const std::size_t part_count = push_payload_parts(walk.parts, node);
    std::meta::info first_token{};
    for (std::size_t index = part_count; index-- > 0;) {
        const std::meta::info found = visit_payload_node(walk, walk.parts.items[first + index]);
        if (first_token == std::meta::info{}) first_token = found;
        if (is_walk_settled(walk)) break;
    }
    walk.parts.top = first;
    return first_token;
}

// Enters a node one time for each reach.  A node that the walk reached
// owned before is a second copy of what it holds: a second copy of a
// token is one tag twice, and a second copy of a node with no token adds
// nothing.  A type cannot hold itself by value, so an owned node is never
// entered while the walk is inside it.  A path through a pointer can come
// back to a node, and the walk does not enter it again.  Complexity: one
// entry for each distinct node, times the scan of the entered nodes.
consteval std::meta::info visit_payload_node(PayloadWalk& walk, PayloadNode node) {
    if (is_walk_settled(walk)) return {};
    if (node.reach == PayloadReach::Named) node.in_ct_carrier = false;
    const PayloadVisit* const seen = walk.visits.items;
    const std::size_t seen_count = walk.visits.top;
    for (std::size_t place = 0; place < seen_count; ++place) {
        if (!is_same_payload_node(seen[place].node, node)) continue;
        if (node.reach == PayloadReach::Owned && seen[place].first_token != std::meta::info{}) {
            refuse_payload(walk, PayloadRefusal::DuplicateTag, seen[place].first_token);
        }
        return seen[place].first_token;
    }
    const std::size_t index = walk.visits.top;
    push(walk.visits, PayloadVisit{node, {}});
    const std::meta::info first_token = enter_payload_node(walk, node);
    walk.visits.items[index].first_token = first_token;
    return first_token;
}

// ── The facts of one payload ────────────────────────────────────────

// Each result of the walk, in a form that a static data member can hold.
// The type is structural, so it is the value argument of the cell of the
// sealed cache below.  The four sets are reflections of
// PermSet specializations.  A refused payload changes no set and hands
// off nothing.
struct PayloadFacts {
    PayloadRefusal refusal = PayloadRefusal::None;
    std::meta::info refused_type{};
    DelegationCarrier carrier = DelegationCarrier::None;
    bool is_carrier_readable = true;
    std::meta::info carrier_type{};
    std::meta::info sender_requires{};
    std::meta::info sender_gains{};
    std::meta::info receiver_requires{};
    std::meta::info receiver_gains{};
    bool carries_share = false;
    // Each DelegatedSession that the payload holds by value, in a static
    // array.
    const std::meta::info* hand_off_data = nullptr;
    std::size_t hand_off_count = 0;

    [[nodiscard]] consteval std::span<const std::meta::info> hand_offs() const noexcept {
        return {hand_off_data, hand_off_count};
    }
};

// The PermSet of the elements of a stack, in the order of the stack.
[[nodiscard]] consteval std::meta::info payload_perm_set_of(const stack<std::meta::info>& elements) {
    return std::meta::substitute(^^::foundation::permissions::PermSet,
                                 std::span<const std::meta::info>{elements.items, elements.top});
}

[[nodiscard]] consteval std::meta::info payload_wrap_each(std::meta::info wrapper, const stack<std::meta::info>& tags) {
    stack<std::meta::info> wrapped{};
    for (std::size_t place = 0; place < tags.top; ++place)
        push(wrapped, std::meta::substitute(wrapper, {tags.items[place]}));
    return payload_perm_set_of(wrapped);
}

// The walk of one payload, and its four sets.  The sender loses what it
// must hold, and the recipient loses what it must hold, so two sets name
// each side's loss too:
//
//   sender requires   moved, lent, and BorrowedIn<t> for each released t
//   sender gains      LentOut<t> for each lent t
//   receiver requires LentOut<t> for each released t
//   receiver gains    moved, BorrowedIn<t> for each lent t, and released
[[nodiscard]] consteval PayloadFacts payload_facts(std::meta::info payload) {
    namespace refl = ::foundation::reflect;
    PayloadWalk walk;
    // A payload typed as a reference names an object that stays with the
    // sender, so its tokens are reached aliased, as through a reference
    // member.
    if (std::meta::is_reference_type(std::meta::dealias(payload))) {
        const refl::TypeNode reached = refl::node_reached_indirectly(payload, refl::SpecializationRead::Instantiating);
        static_cast<void>(
            visit_payload_node(walk, PayloadNode{reached.type, reached.may_read_members, PayloadReach::Aliased}));
    } else {
        static_cast<void>(visit_payload_node(walk, PayloadNode{refl::bare_type(payload), true, PayloadReach::Owned}));
    }
    const PayloadCensus& census = walk.census;
    PayloadFacts facts{census.refusal, census.refused_type, census.carrier, census.is_carrier_readable,
                       census.carrier_type};
    if (census.refusal != PayloadRefusal::None) {
        const std::meta::info empty = std::meta::dealias(^^::foundation::permissions::EmptyPermSet);
        facts.sender_requires = facts.sender_gains = facts.receiver_requires = facts.receiver_gains = empty;
        return facts;
    }
    stack<std::meta::info> sender_requires{};
    stack<std::meta::info> receiver_gains{};
    for (std::size_t place = 0; place < census.moved.top; ++place) {
        push(sender_requires, census.moved.items[place]);
        push(receiver_gains, census.moved.items[place]);
    }
    for (std::size_t place = 0; place < census.lent.top; ++place) {
        const std::meta::info tag = census.lent.items[place];
        push(sender_requires, tag);
        push(receiver_gains, std::meta::substitute(^^BorrowedIn, {tag}));
    }
    for (std::size_t place = 0; place < census.released.top; ++place) {
        const std::meta::info tag = census.released.items[place];
        push(sender_requires, std::meta::substitute(^^BorrowedIn, {tag}));
        push(receiver_gains, tag);
    }
    facts.sender_requires = payload_perm_set_of(sender_requires);
    facts.sender_gains = payload_wrap_each(^^LentOut, census.lent);
    facts.receiver_requires = payload_wrap_each(^^LentOut, census.released);
    facts.receiver_gains = payload_perm_set_of(receiver_gains);
    facts.carries_share = census.carries_share;
    const std::span<const std::meta::info> hand_offs =
        std::define_static_array(std::span<const std::meta::info>{census.hand_offs.items, census.hand_offs.top});
    facts.hand_off_data = hand_offs.data();
    facts.hand_off_count = hand_offs.size();
    return facts;
}

// ── The sealed cache ────────────────────────────────────────────────
//
// Every verdict of this header comes from payload_facts_of, and a user
// cannot change what it returns.  payload_facts is not a template, so no
// explicit specialization of it exists.  The cache is an alias template,
// which cannot be specialized either, and the reader takes the facts from
// the value argument of the cell that the alias names.  A specialization
// of the cell class changes its body, which no reader reads.  GCC keeps
// one specialization of an alias template for each argument, so a
// translation unit walks each payload type one time.

// The cell of the facts of one payload.  It holds nothing.
template <PayloadFacts Facts>
struct payload_facts_cell {};

template <class P>
using payload_facts_cached = payload_facts_cell<payload_facts(^^P)>;

// The facts of a payload.  Complexity: one walk for each payload type in
// a translation unit, and a lookup after that.
[[nodiscard]] consteval PayloadFacts payload_facts_of(std::meta::info payload) {
    const std::meta::info cell = std::meta::dealias(std::meta::substitute(^^payload_facts_cached, {payload}));
    return std::meta::extract<PayloadFacts>(std::meta::template_arguments_of(cell)[0]);
}

// The refusal and its type, for the text of a diagnostic.
struct PayloadVerdict {
    PayloadRefusal refusal = PayloadRefusal::None;
    std::meta::info refused_type{};
};

[[nodiscard]] consteval std::string_view payload_refusal_reason(PayloadRefusal why) noexcept {
    switch (why) {
        case PayloadRefusal::None:
            return "";
        case PayloadRefusal::TokenBehindPointer:
            return "it reaches a permission token through a pointer or a reference.  The token stays with the sender, "
                   "so the recipient would hold a second name for it.  Move the token by value";
        case PayloadRefusal::TokenInUnion:
            return "it holds a permission token in a union, a std::optional or a std::variant.  The token can be "
                   "absent at run time, and the set change cannot depend on a value";
        case PayloadRefusal::TokenInArray:
            return "it holds a permission token in an array.  That is several tokens of one tag, and a permission "
                   "set holds each tag once";
        case PayloadRefusal::DuplicateTag:
            return "it names one permission tag twice.  A permission set holds each tag once";
        case PayloadRefusal::BareBorrowOrShare:
            return "it holds a read proof, a share or a pool outside its marker.  A read proof travels only as "
                   "Borrowed<T, Tag>, and a share only as SharedReader<Tag>, so that a set records the loan";
        case PayloadRefusal::TypeErasure:
            return "it holds a type-erasure family.  The static type does not name what it holds, so a token "
                   "inside it would move with no set change";
        case PayloadRefusal::UnreadableState:
            return "it holds a class whose state the walk cannot read, such as a lambda with captures";
        case PayloadRefusal::IncompleteType:
            return "it reaches a class or a template that is only declared, so nothing says what it holds";
        case PayloadRefusal::ClassifiedBare:
            return "it holds a fixy::Secret outside DeclassifyOnSend.  A classified value on a channel leaves "
                   "classification, and that needs a named policy.  Carry it as DeclassifyOnSend<T, Policy>";
        case PayloadRefusal::ConstantTimeBare:
            return "it holds a constant-time value outside CTPayload.  A bare value offers == and element access, "
                   "which can branch on the content.  Carry it as CTPayload<T>";
        case PayloadRefusal::BareEndpoint:
            return "it holds, points at or names a session endpoint outside DelegatedSession.  A bare endpoint "
                   "moves its permission set with no set change, and the rules of fixy/session/Delegate.h do not "
                   "run.  Hand the endpoint over with mint_delegated_session";
        case PayloadRefusal::HandOffNotOwned:
            return "it reaches a DelegatedSession through a pointer, a reference, a union or an array.  A hand-off "
                   "travels by value, one time, so that one recipient holds the endpoint and its tokens";
        default:
            break;
    }
    return "";
}

[[nodiscard]] consteval std::string_view payload_refusal_text(PayloadVerdict verdict) {
    if (verdict.refusal == PayloadRefusal::None) return {};
    std::string text{"fixy::session::diagnostic [Payload_Refused]: the payload cannot travel on a session channel, "
                     "because "};
    text += payload_refusal_reason(verdict.refusal);
    text += ".  The refused type: ";
    text += std::meta::display_string_of(verdict.refused_type);
    return std::define_static_string(text);
}

// ── The gates ───────────────────────────────────────────────────────
//
// A gate holds the text of a compile error, and a public spelling reads
// its member holds beside the facts.  A specialization of a gate can drop
// the text, or set holds to false, which refuses more.  It cannot admit a
// payload, because each verdict reads the facts.

// A class that is only declared is a compile error and not a refusal
// value.  A unit that defines the class gives a different verdict, and a
// program must not hold two verdicts on one type.
template <class P>
struct payload_readable_gate {
    static constexpr PayloadFacts facts = payload_facts_of(^^P);
    static_assert(facts.refusal != PayloadRefusal::IncompleteType,
                  payload_refusal_text(PayloadVerdict{facts.refusal, facts.refused_type}));
    static constexpr bool holds = true;
};

// A payload that the walk refuses is a compile error where a caller needs
// its sets.
template <class P>
struct payload_admitted_gate {
    static constexpr PayloadFacts facts = payload_facts_of(^^P);
    static_assert(payload_readable_gate<P>::holds);
    static_assert(facts.refusal == PayloadRefusal::None,
                  payload_refusal_text(PayloadVerdict{facts.refusal, facts.refused_type}));
    static constexpr bool holds = true;
};

// True when the walk accepts the payload.
[[nodiscard]] consteval bool payload_is_admitted(std::meta::info payload) {
    return payload_facts_of(payload).refusal == PayloadRefusal::None;
}

// True when the walk accepts the payload, and it moves, lends or releases
// nothing and carries no share.
[[nodiscard]] consteval bool payload_is_plain(std::meta::info payload) {
    const PayloadFacts facts = payload_facts_of(payload);
    return facts.refusal == PayloadRefusal::None && std::meta::template_arguments_of(facts.sender_requires).empty()
        && std::meta::template_arguments_of(facts.receiver_gains).empty() && !facts.carries_share;
}

// The function is a template only so that its text can depend on Anchor
// (foundation/reflect/Anchor.h).  No check reads the text.
template <class Anchor = void>
[[nodiscard]] consteval std::string_view unreadable_component_text(std::meta::info type) {
    ::foundation::reflect::anchored_t<^^Anchor, std::string> text{
        "fixy::session::diagnostic [Payload_Delegation_Unreadable]: the payload reaches "};
    text += std::meta::display_string_of(type);
    text += ", which has no definition here, so the delegation query cannot read whether it holds a session "
            "endpoint.  Define it before the query, or send a payload that does not reach it.";
    return std::define_static_string(text);
}

// A class that the delegation query cannot read is a compile error and
// not a value.  A class can be complete in one translation unit and only
// declared in a different one, and a value would then differ between two
// units of one program.
template <class P>
struct delegation_readable_gate {
    static constexpr PayloadFacts facts = payload_facts_of(^^P);
    static_assert(facts.is_carrier_readable,
                  unreadable_component_text<::foundation::reflect::anchored_t<^^P, void>>(facts.carrier_type));
    static constexpr bool holds = true;
};

// The sets of an admitted payload, for the handle.  Each reads the facts,
// and no class template stands between the facts and the handle.
template <class P>
using sender_requires_t = [:payload_facts_of(^^P).sender_requires:];
template <class P>
using sender_gains_t = [:payload_facts_of(^^P).sender_gains:];
template <class P>
using receiver_requires_t = [:payload_facts_of(^^P).receiver_requires:];
template <class P>
using receiver_gains_t = [:payload_facts_of(^^P).receiver_gains:];

}  // namespace detail

// ── The classification predicates ───────────────────────────────────
//
// Each predicate is a concept over the facts, so no user specialization
// changes it.  A class template of the same name is the one-argument
// form that an armed cell reads.  It reads the concept, and no verdict of
// the session layer reads it.

// True when the walk accepts the payload.  A refused payload answers
// false here, and the refusal text comes from payload_perm_delta.
template <class P>
concept is_permission_classified_v = detail::payload_readable_gate<P>::holds && detail::payload_is_admitted(^^P);

template <class P>
struct is_permission_classified : std::bool_constant<is_permission_classified_v<P>> {};

}  // namespace fixy::session
