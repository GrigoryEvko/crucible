#pragma once

// What a message does to the permission sets of its two endpoints.
//
// A session endpoint holds a set of exclusive permissions, a PermSet.  A
// payload can carry tokens, so each send and each receive changes the
// set of the endpoint that does it.  This header gives that change for
// one payload.  The handle applies it at each Send and each Recv.
//
// The markers:
//
//   Transferable<T, Tag>     The sender gives the token to the recipient.
//   Returned<T, Tag>         The same move.  The type records that it
//                            closes an exclusive loan.
//   Borrowed<T, Tag>         A read loan.  The sender keeps the token
//                            parked and cannot use it until the loan
//                            ends.  The recipient gets the ReadLoan.
//   Released<T, Tag>         The end of a read loan.  It carries the
//                            ReadLoan back, and the lender gets its token
//                            back only with that loan.
//   DelegatedSession<P, R, Pol, PS>
//                            A live endpoint of another session, at
//                            protocol P over Resource R
//                            (fixy/session/Delegate.h).  The tokens in PS
//                            move with it.
//   SharedReader<Tag>        A read share of a pool.  No set changes,
//                            because the pool counts its shares.
//   PeerMsg<Q, L, U>, Labelled<L, U>
//                            A keyed message.  Its payload U changes the
//                            sets as a member of the message does.  The
//                            value step of the handle moves U after the
//                            label word (fixy/session/Handle.h).
//
// A bare Permission in a payload moves as a Transferable does.
//
// ── A read loan is two states, not a free copy ──────────────────────
//
// A read proof that travels to a different thread lets the recipient
// read the region while the sender continues.  So the sender must not
// write the region, or give it away, until the proof comes back.  The
// set records this.  A send of Borrowed<T, Tag> takes Tag out of the
// sender's set and puts LentOut<Tag> in its place, and the recipient's
// set takes BorrowedIn<Tag>.  A send of Released<T, Tag> reverses both.
// While LentOut<Tag> is in the set, no send can move Tag or lend it
// again, because Tag is not in the set.  That is the rule of Saffrich,
// Spaderna, Thiemann and Vasconcelos, "Borrowing from Session Types",
// OOPSLA 2025, errata: a borrowed prefix on a different thread stops the
// suffix until something consumes the prefix.
//
// A protocol that ends with LentOut or BorrowedIn in a set leaves a loan
// open.  The handle refuses to close in that state.
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

#include <foundation/Brand.h>
#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/algebra/Transition.h>
#include <foundation/contracts/Armed.h>
#include <foundation/contracts/Pre.h>
#include <foundation/permissions/PermSet.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>
#include <foundation/reflect/TypeComponents.h>
#include <fixy/session/Classified.h>
#include <fixy/session/Protocol.h>

#include <any>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <meta>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
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

// The door that does each transition of a PermHold, defined below.  It is
// the one friend of the hold and of the loan markers.
class HoldFactory;

// ── The markers ─────────────────────────────────────────────────────
//
// Every marker is move-only.  A copy of a token is a second owner, and
// a copy of a read proof is a second loan that no set records.

template <class T, class Tag>
struct [[nodiscard]] Transferable {
    using payload_type = T;
    using transferred_perm = Tag;

    T value;
    [[no_unique_address]] ::foundation::permissions::Permission<Tag> perm;

    // A protocol names the tag and not the instance, so the message drops
    // the brand of the token through the one door that drops a brand.
    template <class Brand>
    constexpr Transferable(T v, ::foundation::permissions::Permission<Tag, Brand>&& p) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : value{std::move(v)}, perm{::foundation::permissions::permission_erase_brand(std::move(p))} {}

    Transferable(const Transferable&) = delete("Transferable carries a linear token. A copy is a second owner");
    Transferable&
    operator=(const Transferable&) = delete("Transferable carries a linear token. A copy is a second owner");
    constexpr Transferable(Transferable&&) noexcept = default;
    constexpr Transferable& operator=(Transferable&&) noexcept = default;
    ~Transferable() = default;
};

template <class T, class Tag>
struct [[nodiscard]] Returned {
    using payload_type = T;
    using returned_perm = Tag;

    T value;
    [[no_unique_address]] ::foundation::permissions::Permission<Tag> perm;

    // The brand drops as it does for Transferable.
    template <class Brand>
    constexpr Returned(T v, ::foundation::permissions::Permission<Tag, Brand>&& p) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : value{std::move(v)}, perm{::foundation::permissions::permission_erase_brand(std::move(p))} {}

    Returned(const Returned&) = delete("Returned carries a linear token. A copy is a second owner");
    Returned& operator=(const Returned&) = delete("Returned carries a linear token. A copy is a second owner");
    constexpr Returned(Returned&&) noexcept = default;
    constexpr Returned& operator=(Returned&&) noexcept = default;
    ~Returned() = default;
};

// The loan comes from mint_read_loan, which parks the token of the tag.
// There is no constructor that takes no loan, so a Borrowed cannot exist
// without a parked token.  The loan is private: the recipient reaches it
// only through accept_loan, and reads only through the door of its hold.
template <class T, class Tag>
struct [[nodiscard]] Borrowed {
    using payload_type = T;
    using borrowed_perm = Tag;

    T value;

    // The loan is on the erased identity, because the hold that lends it
    // keeps each token on the erased identity of its tag.
    constexpr Borrowed(T v, ::foundation::permissions::ReadLoan<Tag>&& loan) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : value{std::move(v)}, loan_{std::move(loan)} {}

    Borrowed(const Borrowed&) = delete("Borrowed is one read loan. A copy is a second loan that no set records");
    Borrowed&
    operator=(const Borrowed&) = delete("Borrowed is one read loan. A copy is a second loan that no set records");
    constexpr Borrowed(Borrowed&&) noexcept = default;
    Borrowed& operator=(Borrowed&&) = delete("Borrowed holds one read loan for its whole life");
    ~Borrowed() = default;

private:
    friend class HoldFactory;

    [[no_unique_address]] ::foundation::permissions::ReadLoan<Tag> loan_;
};

// The end of a read loan carries the loan back.  The lender gets its
// token only from the parked token and this loan together, so a release
// that no borrower sent cannot reopen the region.  The type-level check
// makes the send sound too: the sender must hold BorrowedIn<Tag>.
template <class T, class Tag>
struct [[nodiscard]] Released {
    using payload_type = T;
    using released_perm = Tag;

    T value;

    // The loan is on the erased identity, as the loan of Borrowed is.
    constexpr Released(T v, ::foundation::permissions::ReadLoan<Tag>&& loan) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : value{std::move(v)}, loan_{std::move(loan)} {}

    Released(const Released&) = delete("Released ends one read loan. A copy ends it a second time");
    Released& operator=(const Released&) = delete("Released ends one read loan. A copy ends it a second time");
    constexpr Released(Released&&) noexcept = default;
    constexpr Released& operator=(Released&&) noexcept = default;
    ~Released() = default;

private:
    friend class HoldFactory;

    [[no_unique_address]] ::foundation::permissions::ReadLoan<Tag> loan_;
};

// A reader's share of a pool, which a message can carry to a reader on
// another thread.  It owns the guard, so the share lives exactly as long
// as the reader, and the pool cannot upgrade while one reader lives.  No
// proof is dropped on the way: a read view comes from the guard that
// this object holds, and only inside read.  The writer is the
// Permission that try_upgrade hands
// back, which is linear, so there is one writer and any number of
// readers, as in CLASS (Rocha and Caires, ESOP 2023).
//
// A share moves no tag, so it changes no permission set.  The pool counts
// its shares.
template <class Tag, class Brand = ::foundation::brand::DefaultBrand>
class [[nodiscard]] SharedReader {
public:
    using tag_type = Tag;
    using brand_type = Brand;

    explicit SharedReader(::foundation::permissions::SharedPermissionGuard<Tag, Brand>&& guard) noexcept
        : guard_{std::move(guard)} {
        CRUCIBLE_PRE(guard_.holds_share());
    }

    SharedReader(const SharedReader&) = delete("a SharedReader owns one share of the pool. A copy counts it twice");
    SharedReader&
    operator=(const SharedReader&) = delete("a SharedReader owns one share of the pool. A copy counts it twice");
    SharedReader(SharedReader&&) noexcept = default;
    SharedReader& operator=(SharedReader&&) = delete("a SharedReader binds one share for its whole life");
    ~SharedReader() = default;

    // Runs the body with a read view of the region, and hands the reader
    // back.  The reader is consumed for the call, so the body cannot end
    // the share through a capture while the view lives.  A body that
    // returns nothing gives the reader back, and a body that returns a
    // value gives a pair of that value and the reader.
    template <class Body>
        requires ::foundation::permissions::ReadViewBody<Body, Tag, Brand>
    [[nodiscard]] auto
    read(Body&& body) && noexcept(::foundation::permissions::detail::read_view_door_nothrow_v<
                                  ::foundation::permissions::SharedPermissionGuard<Tag, Brand>, Body>) {
        auto lent = ::foundation::permissions::with_read_view(std::move(guard_), std::forward<Body>(body));
        if constexpr (std::is_same_v<decltype(lent), ::foundation::permissions::SharedPermissionGuard<Tag, Brand>>) {
            return SharedReader{std::move(lent)};
        } else {
            return std::pair{std::move(lent.first), SharedReader{std::move(lent.second)}};
        }
    }

    [[nodiscard]] bool holds_share() const noexcept { return guard_.holds_share(); }

private:
    ::foundation::permissions::SharedPermissionGuard<Tag, Brand> guard_;
};

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
// makes is a stack of foundation/algebra/Transition.h, and the walk reads it
// through a pointer.  That header states the costs of a vector in a
// constant evaluation of this GCC 16 build.
using ::foundation::algebra::transition::make_room;
using ::foundation::algebra::transition::push;
using ::foundation::algebra::transition::stack;

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
[[nodiscard]] consteval bool holds_type(const std::meta::info* types, std::size_t count, std::meta::info type) noexcept {
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
    return std::meta::is_type(type) && ::foundation::algebra::transition::is_registered(protocol_registry, type);
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
consteval void record_payload_tag(PayloadWalk& walk, stack<std::meta::info> PayloadCensus::* into,
                                  std::meta::info tag, std::meta::info at) {
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
            parts.items[parts.top++] =
                PayloadNode{refl::bare_type(member_type), true, part_reach, node.in_ct_carrier};
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

// The region a set element names: Tag for LentOut<Tag> and BorrowedIn<Tag>,
// and the element itself for any other tag.
[[nodiscard]] consteval std::meta::info region_of(std::meta::info element) {
    const std::meta::info bare = std::meta::dealias(element);
    if (payload_family_is(bare, ^^LentOut) || payload_family_is(bare, ^^BorrowedIn)) {
        return std::meta::dealias(std::meta::template_arguments_of(bare)[0]);
    }
    return bare;
}

// True when no element of `gains` names a region that an element of
// `kept` already names.  One region in two states at once is two claims
// on it, which a set must not hold.
[[nodiscard]] consteval bool regions_disjoint(std::meta::info kept, std::meta::info gains) {
    const std::vector<std::meta::info> gained_elements = std::meta::template_arguments_of(std::meta::dealias(gains));
    const std::size_t gained_count = gained_elements.size();
    if (gained_count == 0) return true;
    const std::vector<std::meta::info> held_elements = std::meta::template_arguments_of(std::meta::dealias(kept));
    const std::size_t held_count = held_elements.size();
    const std::meta::info* const gained = gained_elements.data();
    const std::meta::info* const held = held_elements.data();
    for (std::size_t gained_place = 0; gained_place < gained_count; ++gained_place) {
        const std::meta::info gained_region = region_of(gained[gained_place]);
        for (std::size_t held_place = 0; held_place < held_count; ++held_place) {
            if (gained_region == region_of(held[held_place])) return false;
        }
    }
    return true;
}

[[nodiscard]] consteval std::string_view unreadable_component_text(std::meta::info type) {
    std::string text{"fixy::session::diagnostic [Payload_Delegation_Unreadable]: the payload reaches "};
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
    static_assert(facts.is_carrier_readable, unreadable_component_text(facts.carrier_type));
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

// ── The delta ───────────────────────────────────────────────────────
//
// The permission accounting of one payload.  The handle applies it:
// Send<P, K> takes sender_requires from the set, and leaves
//   (set minus sender_requires) plus sender_gains.
// Recv<P, K> takes receiver_requires from the set, and leaves
//   (set minus receiver_requires) plus receiver_gains.
//
// This class is the view of the delta for a reader.  The handle reads
// perm_set_after_send_t and perm_set_after_recv_t below, which read the
// facts, so a specialization of this class changes no step.
template <class P>
struct payload_perm_delta {
    static_assert(detail::payload_admitted_gate<P>::holds);

    using sender_requires = detail::sender_requires_t<P>;
    using sender_loses = sender_requires;
    using sender_gains = detail::sender_gains_t<P>;
    using receiver_requires = detail::receiver_requires_t<P>;
    using receiver_loses = receiver_requires;
    using receiver_gains = detail::receiver_gains_t<P>;

    // A share moves no tag, so it changes no set.  The flag is here for
    // a handle that wants to know a share went by.
    static constexpr bool carries_share = detail::payload_facts_of(^^P).carries_share;
};

// True when the payload moves, lends or releases nothing and carries no
// share.  Such a payload leaves both sets as they were.
template <class P>
concept is_plain_payload_v = detail::payload_readable_gate<P>::holds && detail::payload_is_plain(^^P);

template <class P>
struct is_plain_payload : std::bool_constant<is_plain_payload_v<P>> {};

// ── Delegation ──────────────────────────────────────────────────────
//
// The component of P that delegates first, in the order of the walk, or
// None.  The head of this header states which components delegate.  This
// value is the view for a reader.  No verdict reads it.
template <class P>
inline constexpr DelegationCarrier payload_delegation_carrier_v =
    detail::delegation_readable_gate<P>::holds ? detail::payload_facts_of(^^P).carrier
                                               : DelegationCarrier::UnreadableState;

// True when P gives the recipient authority over a session endpoint.  A
// crash session and a checkpoint session refuse such a payload.  A gate
// that does not hold answers true here, which refuses more.
template <class P>
concept payload_conveys_delegation_v =
    !detail::delegation_readable_gate<P>::holds || detail::payload_facts_of(^^P).carrier != DelegationCarrier::None;

namespace detail {

// The one-argument form of the query, so that it can hold an armed cell.
template <class P>
struct payload_conveys_delegation : std::bool_constant<payload_conveys_delegation_v<P>> {};

}  // namespace detail

// ── The protocol walk ───────────────────────────────────────────────
//
// The walks below read a protocol through the registry of
// fixy/session/Protocol.h: each node of the spine one time, the node
// first, then its continuation, then each branch.  A payload is not a
// node.  Each walk reads a payload through payload_facts_of, so a payload
// has one walk.

namespace detail {

// Calls visit on each node of the spine of `protocol` that `visited` does
// not hold, and adds the node to `visited`.  A node that the registry does
// not know goes to visit and is not entered.  visit returns false to stop
// the walk, and then the function returns false.  Complexity: one visit
// for each distinct node, times the scan of `visited`.
template <class Visitor>
consteval bool walk_protocol_spine(std::meta::info protocol, stack<std::meta::info>& visited, Visitor& visit) {
    namespace tr = ::foundation::algebra::transition;
    const std::meta::info type = std::meta::dealias(protocol);
    if (holds_type(visited, type)) return true;
    push(visited, type);
    const tr::node view = tr::decompose(protocol_registry, type);
    if (!visit(view)) return false;
    if (!view.is_registered) return true;
    if (view.next != std::meta::info{} && !walk_protocol_spine(view.next, visited, visit)) return false;
    for (const std::meta::info branch : view.branches) {
        if (!walk_protocol_spine(branch, visited, visit)) return false;
    }
    return true;
}

}  // namespace detail

// ── Delegation to a peer of the delegated session ───────────────────
//
// A send that hands an endpoint of a session S to a role that is a peer
// of S gives that role two endpoints of S: its own end and the end that
// it receives.  The role can then wait on one end for a message that
// only the other end sends, and S deadlocks.  This breaks the forest
// condition of LinearActris (Jacobs, Hinrichsen and Krebbers, POPL 2024).
//
// A local type names the receiver of each message with a PeerMsg.  So
// the check reads each Send of a PeerMsg<Q, L, U>, and each
// DelegatedSession<Inner, ...> that U hands off, and refuses the protocol
// when Inner names Q as a peer.  The peers of Inner are the peer of each
// PeerMsg and the role of each Sender note in it.  A binary step names
// no receiver, so the check cannot see a delegation over it.  There the
// watch of fixy/session/Watch.h refuses the wait of a thread that holds
// the peer of the endpoint it waits on.
//
// The walk does not go into a payload, so the protocol of a delegated
// endpoint is a different session, and its own mint checks it.

namespace detail {

// Adds to `peers` each role that a protocol names as a peer and that
// `peers` does not hold: the peer of each PeerMsg that a step carries, and
// the role of each Sender note of a choice.  Complexity: one visit for each
// distinct node, times the scans of `peers` and of the visited nodes.
consteval void collect_named_peers(std::meta::info protocol, stack<std::meta::info>& peers) {
    namespace tr = ::foundation::algebra::transition;
    stack<std::meta::info> visited{};
    auto collect = [&peers](const tr::node& view) consteval {
        if (!view.is_registered) return true;
        if (view.entry.kind == tr::shape_kind::step && payload_family_is(view.payload, ^^PeerMsg)) {
            const std::meta::info peer = std::meta::dealias(std::meta::template_arguments_of(view.payload)[0]);
            if (!holds_type(peers, peer)) push(peers, peer);
        }
        if (view.annotation != std::meta::info{} && payload_family_is(view.annotation, ^^Sender)) {
            const std::meta::info role = std::meta::dealias(std::meta::template_arguments_of(view.annotation)[0]);
            if (!holds_type(peers, role)) push(peers, role);
        }
        return true;
    };
    static_cast<void>(walk_protocol_spine(protocol, visited, collect));
}

// True when a Send of the protocol hands an endpoint of a session to a
// role that the protocol of that session names as a peer.
[[nodiscard]] consteval bool delegates_to_own_peer(std::meta::info protocol) {
    namespace tr = ::foundation::algebra::transition;
    bool is_found = false;
    stack<std::meta::info> visited{};
    auto check = [&is_found](const tr::node& view) consteval {
        const bool is_send = view.is_registered && view.entry.kind == tr::shape_kind::step
                          && view.entry.direction == tr::polarity::output;
        if (!is_send || !payload_family_is(view.payload, ^^PeerMsg)) return true;
        const std::vector<std::meta::info> parts = std::meta::template_arguments_of(view.payload);
        const std::meta::info* const part = parts.data();
        const std::meta::info receiver = std::meta::dealias(part[0]);
        for (const std::meta::info carried : payload_facts_of(part[2]).hand_offs()) {
            stack<std::meta::info> peers{};
            collect_named_peers(std::meta::template_arguments_of(carried)[0], peers);
            if (holds_type(peers, receiver)) {
                is_found = true;
                return false;
            }
        }
        return true;
    };
    static_cast<void>(walk_protocol_spine(protocol, visited, check));
    return is_found;
}

}  // namespace detail

template <class Proto>
concept delegates_to_own_peer_v = detail::delegates_to_own_peer(^^Proto);

// The gate that each mint of fixy/session/Handle.h reads.
template <class Proto>
concept DelegatesToNoOwnPeer = !delegates_to_own_peer_v<Proto>;

// ── The regions that a protocol delivers ────────────────────────────
//
// A receive adds to the set of the receiver each region that its payload
// moves, lends or releases (receiver_gains of payload_perm_delta above).
// The receiver then holds a token, a read loan or a returned token of
// that region.  The row of the tag of a region names the effects that a
// touch of the region incurs (foundation/permissions/Permission.h).  So
// the context of the receiver must admit that row, as the context of a
// mint admits the row of each token that the mint consumes.  The context
// gate of fixy/session/Handle.h reads the set below for that check.
//
// The walk visits each Recv of the protocol, on each branch and in each
// loop body.  A received DelegatedSession gives the receiver an endpoint,
// and the receiver runs the protocol of that endpoint.  So the walk also
// visits each Recv of that protocol, and the tags of the endpoint set
// count as regions that the message moves.  A Send delivers nothing: the
// sender held each region that it sends, and the LentOut state of a loan
// names a region that the sender held.
//
// A node that the registry does not know, and a payload that the payload
// walk refuses, stop the build.  Complexity: one visit for each distinct
// node of the protocol and of each delegated protocol, and one payload
// walk for each distinct received payload.

namespace detail {

// Why the walk stopped: a node that the registry does not know, or a
// payload that the payload walk refuses.  It holds no vector, so a static
// data member can hold it.
struct DeliveryVerdict {
    std::meta::info unregistered_node{};
    PayloadVerdict payload{};
};

struct DeliveryWalk {
    stack<std::meta::info> regions;
    stack<std::meta::info> visited;
    DeliveryVerdict verdict;
};

[[nodiscard]] consteval bool delivery_is_refused(const DeliveryVerdict& verdict) noexcept {
    return verdict.unregistered_node != std::meta::info{} || verdict.payload.refusal != PayloadRefusal::None;
}

// A node gives the same regions on every path that reaches it, so the
// walk visits each node one time, also across the delegated protocols.
consteval void walk_delivered_regions(std::meta::info protocol, DeliveryWalk& walk) {
    namespace tr = ::foundation::algebra::transition;
    auto deliver = [&walk](const tr::node& view) consteval {
        if (!view.is_registered) {
            walk.verdict.unregistered_node = view.type;
            return false;
        }
        if (view.entry.kind != tr::shape_kind::step || view.entry.direction != tr::polarity::input) return true;
        const PayloadFacts facts = payload_facts_of(view.payload);
        if (facts.refusal != PayloadRefusal::None) {
            walk.verdict.payload = PayloadVerdict{facts.refusal, facts.refused_type};
            return false;
        }
        // The set of a delegated endpoint can hold a loan state, and the
        // region of LentOut<Tag> or BorrowedIn<Tag> is Tag.
        const std::vector<std::meta::info> elements = std::meta::template_arguments_of(facts.receiver_gains);
        const std::meta::info* const element = elements.data();
        const std::size_t element_count = elements.size();
        for (std::size_t place = 0; place < element_count; ++place) {
            const std::meta::info region = region_of(element[place]);
            if (!holds_type(walk.regions, region)) push(walk.regions, region);
        }
        for (const std::meta::info carried : facts.hand_offs()) {
            walk_delivered_regions(std::meta::template_arguments_of(carried)[0], walk);
        }
        return !delivery_is_refused(walk.verdict);
    };
    static_cast<void>(walk_protocol_spine(protocol, walk.visited, deliver));
}

// The result of the walk over one protocol: why it stopped, or the
// regions as a PermSet in the order that the walk finds them.  The type is
// structural, so it is the value argument of the cell of its sealed cache,
// as the facts of a payload are.
struct DeliveryFacts {
    DeliveryVerdict verdict{};
    std::meta::info regions{};
};

[[nodiscard]] consteval DeliveryFacts delivery_facts(std::meta::info protocol) {
    DeliveryWalk walk;
    walk_delivered_regions(protocol, walk);
    const std::meta::info regions = delivery_is_refused(walk.verdict)
                                      ? std::meta::dealias(^^::foundation::permissions::EmptyPermSet)
                                      : payload_perm_set_of(walk.regions);
    return DeliveryFacts{walk.verdict, regions};
}

template <DeliveryFacts Facts>
struct delivery_facts_cell {};

template <class Proto>
using delivery_facts_cached = delivery_facts_cell<delivery_facts(^^Proto)>;

// The result of the walk over one protocol.  Complexity: one walk for each
// protocol type in a translation unit, and a lookup after that.
[[nodiscard]] consteval DeliveryFacts delivery_facts_of(std::meta::info protocol) {
    const std::meta::info cell = std::meta::dealias(std::meta::substitute(^^delivery_facts_cached, {protocol}));
    return std::meta::extract<DeliveryFacts>(std::meta::template_arguments_of(cell)[0]);
}

[[nodiscard]] consteval std::string_view delivery_refusal_text(DeliveryVerdict verdict) {
    if (verdict.payload.refusal != PayloadRefusal::None) return payload_refusal_text(verdict.payload);
    if (verdict.unregistered_node == std::meta::info{}) return {};
    return ::foundation::algebra::transition::unregistered_message(unregistered_prefix, verdict.unregistered_node);
}

// A protocol that the walk stops on is a compile error.  The gate has the
// shape of the payload gates above.
template <class Proto>
struct delivery_gate {
    static constexpr DeliveryVerdict verdict = delivery_facts_of(^^Proto).verdict;
    static_assert(!delivery_is_refused(verdict), delivery_refusal_text(verdict));
    static constexpr bool holds = true;
};

// The regions of a protocol whose gate holds.  A gate that does not hold
// gives void, which no reader accepts as a set.
[[nodiscard]] consteval std::meta::info delivered_regions_if(bool is_gate_held, std::meta::info protocol) {
    return is_gate_held ? delivery_facts_of(protocol).regions : ^^void;
}

// True when the set holds an open loan: a LentOut or a BorrowedIn.
// Complexity: linear in the size of the set.
[[nodiscard]] consteval bool holds_open_loan(std::meta::info set) {
    const std::vector<std::meta::info> elements = std::meta::template_arguments_of(std::meta::dealias(set));
    const std::meta::info* const element = elements.data();
    const std::size_t element_count = elements.size();
    for (std::size_t place = 0; place < element_count; ++place) {
        const std::meta::info bare = std::meta::dealias(element[place]);
        if (payload_family_is(bare, ^^LentOut) || payload_family_is(bare, ^^BorrowedIn)) return true;
    }
    return false;
}

}  // namespace detail

// The regions that Proto can deliver to the endpoint that runs it: each
// region that a Recv of Proto, or of a protocol that Proto receives in a
// DelegatedSession, moves, lends or releases to the receiver.  The alias
// is the spelling that the context gate of fixy/session/Handle.h reads.
// The class of the same name is the view for a reader.
template <class Proto>
using protocol_delivered_regions_t = [:detail::delivered_regions_if(detail::delivery_gate<Proto>::holds, ^^Proto):];

template <class Proto>
struct protocol_delivered_regions {
    static constexpr detail::DeliveryVerdict verdict = detail::delivery_facts_of(^^Proto).verdict;
    using type = protocol_delivered_regions_t<Proto>;
};

// ── The set after one step ──────────────────────────────────────────

template <class PS, class P>
using perm_set_after_send_t = ::foundation::permissions::perm_set_union_t<
    ::foundation::permissions::perm_set_difference_t<PS, detail::sender_requires_t<P>>, detail::sender_gains_t<P>>;

template <class PS, class P>
using perm_set_after_recv_t = ::foundation::permissions::perm_set_union_t<
    ::foundation::permissions::perm_set_difference_t<PS, detail::receiver_requires_t<P>>, detail::receiver_gains_t<P>>;

// A sender can send a payload when the walk accepts it, the sender holds
// what the payload takes, and no gained loan state names a region the sender
// still holds in another state.
template <class P, class PS>
concept SendablePayload =
    is_permission_classified_v<P> && ::foundation::permissions::perm_set_subset(^^detail::sender_requires_t<P>, ^^PS)
    && detail::regions_disjoint(^^::foundation::permissions::perm_set_difference_t<PS, detail::sender_requires_t<P>>,
                                ^^detail::sender_gains_t<P>);

// A recipient can receive a payload when the walk accepts it, the
// recipient holds what the payload closes, and nothing it gains names a region
// the recipient already holds in any state.  Two tokens of one region
// in one set are two owners of it.
template <class P, class PS>
concept ReceivablePayload =
    is_permission_classified_v<P> && ::foundation::permissions::perm_set_subset(^^detail::receiver_requires_t<P>, ^^PS)
    && detail::regions_disjoint(^^::foundation::permissions::perm_set_difference_t<PS, detail::receiver_requires_t<P>>,
                                ^^detail::receiver_gains_t<P>);

// True when the set holds an open loan: a LentOut or a BorrowedIn.  A
// handle must not close while one is open.
template <class PS>
concept perm_set_has_open_loan_v = detail::holds_open_loan(^^PS);

// ── The tokens of a set, held ───────────────────────────────────────
//
// A PermHold keeps the token object for each element of its set: the
// Permission of each plain tag, the LentPermission of each LentOut tag,
// and the ReadLoan of each BorrowedIn tag.  Every transition consumes
// the hold and returns the next one, whose set is the set after the
// step.  So the tokens and the set cannot disagree:
//
//   take / put         a token leaves or enters the hold
//   pack / unpack      the same, inside a Transferable
//   lend / end_loan    the token parks in LentOut<Tag>, then returns with
//                      the loan that the release carries back
//   accept_loan /      a read loan enters as BorrowedIn<Tag>, then goes
//     release          back inside the release
//   read               the body reads a BorrowedIn region through the
//                      door of the loan, and the hold comes back
//
// A hold carries one flag of state, which the transitions clear.  A
// transition on a hold that a move or an earlier transition consumed
// aborts, because a second use of it would spend its tokens twice.  A
// live hold that is destroyed with a loan open aborts too.  A lender
// that drops its hold leaves the loan unclosed, and a borrower that
// drops its hold leaves the lender waiting for a release that never
// comes.  That is the silent drop that LinearActris (Jacobs, Hinrichsen,
// Krebbers, POPL 2024) forbids.
//
// Each transition that builds the next hold is a member of HoldFactory,
// and the member of the hold calls it.  HoldFactory is the one friend of
// the hold and of the loan markers.  It is not a template, and this
// header defines it, so no specialization of a template and no class of
// another translation unit reaches the tokens that a hold keeps.

template <class PS>
class PermHold;

namespace detail {

// The object that a hold keeps for one element of its set.
[[nodiscard]] consteval std::meta::info hold_slot_of(std::meta::info element) {
    const std::meta::info bare = std::meta::dealias(element);
    if (payload_family_is(bare, ^^LentOut)) {
        return std::meta::substitute(^^::foundation::permissions::LentPermission,
                                     {std::meta::template_arguments_of(bare)[0]});
    }
    if (payload_family_is(bare, ^^BorrowedIn)) {
        return std::meta::substitute(^^::foundation::permissions::ReadLoan,
                                     {std::meta::template_arguments_of(bare)[0]});
    }
    return std::meta::substitute(^^::foundation::permissions::Permission, {bare});
}

template <class Element>
using hold_slot_t = [:hold_slot_of(^^Element):];

// True when the element is a plain tag, and not a loan state.
template <class Element>
concept PlainTag = !payload_family_is(std::meta::dealias(^^Element), ^^LentOut)
                && !payload_family_is(std::meta::dealias(^^Element), ^^BorrowedIn);

// True when the message moves its token to the recipient: a Transferable
// or a Returned, received as an rvalue.
template <class Message>
concept MovesAToken = payload_family_is(^^Message, ^^Transferable) || payload_family_is(^^Message, ^^Returned);

// The gate of a read loan that a hold of Set lends as a Borrowed of T,
// and of a read loan that a hold of Set accepts.  HoldFactory and each
// member of PermHold read the same gate.
template <class Set, class Tag, class T>
concept HoldCanLend = SendablePayload<Borrowed<T, Tag>, Set> && ::foundation::permissions::ReadViewNeedsNoCtx<Tag>;

template <class Set, class T, class Tag>
concept HoldCanAcceptLoan = ReceivablePayload<Borrowed<T, Tag>, Set>;

[[noreturn]] CRUCIBLE_COLD inline void hold_consumed_abort_() noexcept {
    std::fputs("fixy::session::diagnostic [Hold_Consumed]: a PermHold was used after a move or a transition "
               "consumed it.  A second use spends its tokens twice.  Use the hold that the transition returned.\n",
               stderr);
    std::abort();
}

[[noreturn]] CRUCIBLE_COLD inline void hold_open_loan_abort_() noexcept {
    std::fputs("fixy::session::diagnostic [Hold_Open_Loan]: a PermHold was destroyed with a loan open.  A lender "
               "that drops its hold leaves the loan unclosed, and a borrower that drops its hold leaves the lender "
               "waiting.  Close the loan with end_loan or release before the hold ends.\n",
               stderr);
    std::abort();
}

struct hold_from_slots {};

}  // namespace detail

// The door that builds every hold.  Each public member takes live tokens
// or a live hold and does one complete transition, with the gate of the
// member of the hold that calls it, so a direct call is no weaker than
// that member.  The class is final, and no object of it exists.
// foundation/NoObject.h tells why no byte route makes one.
class HoldFactory final : ::foundation::NoObject<HoldFactory> {
    struct no_incoming {};

    // The slot that a hold keeps for one element of its set.
    template <class Element, class Set>
    [[nodiscard]] static constexpr auto& slot_(PermHold<Set>& hold) noexcept {
        return std::get<PermHold<Set>::template index_of_<Element>>(hold.slots_);
    }

    // The slot of one element of the next set: the slot that the hold
    // keeps for it, or the incoming object when the element is new.  A
    // transition adds at most one element, so at most one element takes
    // the incoming object.
    template <class Element, class Set, class Incoming>
    [[nodiscard]] static constexpr detail::hold_slot_t<Element> pick_(PermHold<Set>& from,
                                                                      Incoming&& incoming) noexcept {
        if constexpr (::foundation::permissions::perm_set_contains(^^Set, ^^Element)) {
            return std::move(slot_<Element>(from));
        } else {
            return detail::hold_slot_t<Element>{std::forward<Incoming>(incoming)};
        }
    }

    // Consumes the hold and builds the hold of the set Next from its slots
    // and the incoming object.
    template <class Next, class Set, class Incoming = no_incoming>
    [[nodiscard]] static constexpr PermHold<Next> transition_(PermHold<Set>& from, Incoming&& incoming = {}) noexcept {
        from.live_ = false;
        return [&]<class... Following>(std::type_identity<::foundation::permissions::PermSet<Following...>>) {
            return PermHold<Next>{detail::hold_from_slots{},
                                  pick_<Following>(from, std::forward<Incoming>(incoming))...};
        }(std::type_identity<Next>{});
    }

public:
    // Consumes the tokens and builds the hold of their set.  The gate is
    // the gate of mint_permission_hold.
    template <class... Tags, class... Brands>
        requires ::foundation::permissions::DistinctTags<Tags...>
    [[nodiscard]] static constexpr PermHold<::foundation::permissions::PermSet<Tags...>>
    from_tokens(::foundation::permissions::Permission<Tags, Brands>... tokens) noexcept {
        return PermHold<::foundation::permissions::PermSet<Tags...>>{
            detail::hold_from_slots{}, ::foundation::permissions::permission_erase_brand(std::move(tokens))...};
    }

    template <class Tag, class Set>
        requires(::foundation::permissions::perm_set_contains(^^Set, ^^Tag) && detail::PlainTag<Tag>)
    [[nodiscard]] static constexpr auto take(PermHold<Set>&& hold) noexcept
        -> std::pair<::foundation::permissions::Permission<Tag>,
                     PermHold<::foundation::permissions::perm_set_remove_t<Set, Tag>>> {
        hold.require_live_();
        ::foundation::permissions::Permission<Tag> token = std::move(slot_<Tag>(hold));
        auto rest = transition_<::foundation::permissions::perm_set_remove_t<Set, Tag>>(hold);
        return {std::move(token), std::move(rest)};
    }

    template <class Set, class Tag, class Brand>
        requires(detail::PlainTag<Tag> && detail::regions_disjoint(^^Set, ^^::foundation::permissions::PermSet<Tag>))
    [[nodiscard]] static constexpr auto put(PermHold<Set>&& hold,
                                            ::foundation::permissions::Permission<Tag, Brand> token) noexcept
        -> PermHold<::foundation::permissions::perm_set_insert_t<Set, Tag>> {
        hold.require_live_();
        return transition_<::foundation::permissions::perm_set_insert_t<Set, Tag>>(
            hold, ::foundation::permissions::permission_erase_brand(std::move(token)));
    }

    template <class Set, class Message>
        requires detail::MovesAToken<Message> && ReceivablePayload<Message, Set>
    [[nodiscard]] static constexpr auto
    unpack(PermHold<Set>&& hold,
           Message&& message) noexcept(std::is_nothrow_move_constructible_v<typename Message::payload_type>)
        -> std::pair<typename Message::payload_type, PermHold<perm_set_after_recv_t<Set, Message>>> {
        hold.require_live_();
        typename Message::payload_type value = std::move(message.value);
        auto next = transition_<perm_set_after_recv_t<Set, Message>>(hold, std::move(message.perm));
        return {std::move(value), std::move(next)};
    }

    // The token parks in the LentOut slot, where take cannot reach it,
    // and the loan leaves inside the Borrowed.  Only a release that
    // carries the same loan back unparks the token.
    template <class Tag, class Set, class T>
        requires detail::HoldCanLend<Set, Tag, T>
    [[nodiscard]] static constexpr auto lend(PermHold<Set>&& hold,
                                             T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        -> std::pair<Borrowed<T, Tag>, PermHold<perm_set_after_send_t<Set, Borrowed<T, Tag>>>> {
        hold.require_live_();
        auto [loan, parked] = ::foundation::permissions::mint_read_loan(std::move(slot_<Tag>(hold)));
        Borrowed<T, Tag> message{std::move(value), std::move(loan)};
        auto next = transition_<perm_set_after_send_t<Set, Borrowed<T, Tag>>>(hold, std::move(parked));
        return {std::move(message), std::move(next)};
    }

    template <class Set, class T, class Tag>
        requires ReceivablePayload<Released<T, Tag>, Set>
    [[nodiscard]] static constexpr auto
    end_loan(PermHold<Set>&& hold, Released<T, Tag>&& message) noexcept(std::is_nothrow_move_constructible_v<T>)
        -> std::pair<T, PermHold<perm_set_after_recv_t<Set, Released<T, Tag>>>> {
        hold.require_live_();
        T value = std::move(message.value);
        ::foundation::permissions::LentPermission<Tag>& parked = slot_<LentOut<Tag>>(hold);
        auto next = transition_<perm_set_after_recv_t<Set, Released<T, Tag>>>(
            hold, ::foundation::permissions::mint_permission_after_loan(std::move(parked), std::move(message.loan_)));
        return {std::move(value), std::move(next)};
    }

    template <class Set, class T, class Tag>
        requires detail::HoldCanAcceptLoan<Set, T, Tag>
    [[nodiscard]] static constexpr auto
    accept_loan(PermHold<Set>&& hold, Borrowed<T, Tag>&& message) noexcept(std::is_nothrow_move_constructible_v<T>)
        -> std::pair<T, PermHold<perm_set_after_recv_t<Set, Borrowed<T, Tag>>>> {
        hold.require_live_();
        T value = std::move(message.value);
        auto next = transition_<perm_set_after_recv_t<Set, Borrowed<T, Tag>>>(hold, std::move(message.loan_));
        return {std::move(value), std::move(next)};
    }

    template <class Tag, class Set, class T>
        requires SendablePayload<Released<T, Tag>, Set>
    [[nodiscard]] static constexpr auto release(PermHold<Set>&& hold,
                                                T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        -> std::pair<Released<T, Tag>, PermHold<perm_set_after_send_t<Set, Released<T, Tag>>>> {
        hold.require_live_();
        Released<T, Tag> message{std::move(value), std::move(slot_<BorrowedIn<Tag>>(hold))};
        auto next = transition_<perm_set_after_send_t<Set, Released<T, Tag>>>(hold);
        return {std::move(message), std::move(next)};
    }
};

// Consumes the tokens and returns the hold of their set.  Each token is
// a parameter by value, so an lvalue token is refused: it would leave a
// second name for a token that the hold now owns.  One tag twice is
// refused, because a set holds each tag once.  The brand of each token is
// erased, so the hold names regions by tag, as a permission set does.
template <class... Tags, class... Brands>
    requires ::foundation::permissions::DistinctTags<Tags...>
[[nodiscard]] constexpr PermHold<::foundation::permissions::PermSet<Tags...>>
mint_permission_hold(::foundation::permissions::Permission<Tags, Brands>... tokens) noexcept {
    return HoldFactory::from_tokens(std::move(tokens)...);
}

template <class... Elems>
class [[nodiscard]] PermHold<::foundation::permissions::PermSet<Elems...>> {
    using Set = ::foundation::permissions::PermSet<Elems...>;

public:
    using perm_set = Set;

    PermHold(const PermHold&) = delete("a PermHold owns its tokens. A copy owns them twice");
    PermHold& operator=(const PermHold&) = delete("a PermHold owns its tokens. A copy owns them twice");
    PermHold& operator=(PermHold&&) = delete("a transition returns the next hold. Bind it to a new name");

    constexpr PermHold(PermHold&& other) noexcept
        : slots_{std::move(other.slots_)}, live_{std::exchange(other.live_, false)} {}

    constexpr ~PermHold() {
        if (live_ && perm_set_has_open_loan_v<Set>) detail::hold_open_loan_abort_();
    }

    // True while no move and no transition consumed this hold.
    [[nodiscard]] constexpr bool is_live() const noexcept { return live_; }

    // ── A token leaves or enters the hold ──────────────────────────

    template <class Tag>
        requires(::foundation::permissions::perm_set_contains(^^Set, ^^Tag) && detail::PlainTag<Tag>)
    [[nodiscard]] constexpr auto take() && noexcept {
        return HoldFactory::take<Tag>(std::move(*this));
    }

    template <class Tag, class Brand>
        requires(detail::PlainTag<Tag> && detail::regions_disjoint(^^Set, ^^::foundation::permissions::PermSet<Tag>))
    [[nodiscard]] constexpr auto put(::foundation::permissions::Permission<Tag, Brand> token) && noexcept {
        return HoldFactory::put(std::move(*this), std::move(token));
    }

    // ── Moves inside a message ─────────────────────────────────────

    template <class Tag, class T>
        requires(::foundation::permissions::perm_set_contains(^^Set, ^^Tag) && detail::PlainTag<Tag>)
    [[nodiscard]] constexpr auto pack(T value) && noexcept(std::is_nothrow_move_constructible_v<T>)
        -> std::pair<Transferable<T, Tag>, PermHold<::foundation::permissions::perm_set_remove_t<Set, Tag>>> {
        auto [token, rest] = std::move(*this).template take<Tag>();
        return {Transferable<T, Tag>{std::move(value), std::move(token)}, std::move(rest)};
    }

    // Receives a Transferable or a Returned, as an rvalue.
    template <class Message>
        requires detail::MovesAToken<Message> && ReceivablePayload<Message, Set>
    [[nodiscard]] constexpr auto
    unpack(Message&& message) && noexcept(std::is_nothrow_move_constructible_v<typename Message::payload_type>) {
        return HoldFactory::unpack(std::move(*this), std::move(message));
    }

    // ── A read loan, lender side ───────────────────────────────────

    template <class Tag, class T>
        requires detail::HoldCanLend<Set, Tag, T>
    [[nodiscard]] constexpr auto lend(T value) && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return HoldFactory::lend<Tag>(std::move(*this), std::move(value));
    }

    template <class T, class Tag>
        requires ReceivablePayload<Released<T, Tag>, Set>
    [[nodiscard]] constexpr auto
    end_loan(Released<T, Tag>&& message) && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return HoldFactory::end_loan(std::move(*this), std::move(message));
    }

    // ── A read loan, borrower side ─────────────────────────────────

    template <class T, class Tag>
        requires detail::HoldCanAcceptLoan<Set, T, Tag>
    [[nodiscard]] constexpr auto
    accept_loan(Borrowed<T, Tag>&& message) && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return HoldFactory::accept_loan(std::move(*this), std::move(message));
    }

    // Runs the body with a read view of a region that this hold borrows,
    // and hands the hold back.  The hold is consumed for the call, so a
    // body that names the hold through a capture finds it spent, and a
    // transition on it aborts.  A body that returns nothing gives the
    // hold back, and a body that returns a value gives a pair of that
    // value and the hold.
    template <class Tag, class Body>
        requires(::foundation::permissions::perm_set_contains(^^Set, ^^BorrowedIn<Tag>))
             && ::foundation::permissions::ReadViewBody<Body, Tag, ::foundation::brand::DefaultBrand>
    [[nodiscard]] constexpr auto read(Body&& body) && noexcept(
        ::foundation::permissions::detail::read_view_door_nothrow_v<::foundation::permissions::ReadLoan<Tag>, Body>) {
        require_live_();
        PermHold held{std::move(*this)};
        ::foundation::permissions::ReadLoan<Tag>& slot = std::get<index_of_<BorrowedIn<Tag>>>(held.slots_);
        auto lent = ::foundation::permissions::with_read_view(std::move(slot), std::forward<Body>(body));
        if constexpr (std::is_same_v<decltype(lent), ::foundation::permissions::ReadLoan<Tag>>) {
            slot = std::move(lent);
            return held;
        } else {
            slot = std::move(lent.second);
            return std::pair{std::move(lent.first), std::move(held)};
        }
    }

    template <class Tag, class T>
        requires SendablePayload<Released<T, Tag>, Set>
    [[nodiscard]] constexpr auto release(T value) && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return HoldFactory::release<Tag>(std::move(*this), std::move(value));
    }

    // ── The end of the hold ────────────────────────────────────────

    // Hands every token back.  A hold with a loan open has no tokens to
    // hand back for the loan, so it cannot end here.
    [[nodiscard]] constexpr std::tuple<::foundation::permissions::Permission<Elems>...> into_permissions() && noexcept
        requires(!perm_set_has_open_loan_v<Set>)
    {
        require_live_();
        live_ = false;
        return std::apply(
            [](auto&... slot) {
                return std::tuple<::foundation::permissions::Permission<Elems>...>{std::move(slot)...};
            },
            slots_);
    }

private:
    friend class HoldFactory;

    template <class... Slots>
    constexpr explicit PermHold(detail::hold_from_slots, Slots&&... slots) noexcept
        : slots_{std::forward<Slots>(slots)...} {}

    constexpr void require_live_() const noexcept {
        if (!live_) [[unlikely]]
            detail::hold_consumed_abort_();
    }

    // The position of an element in the set.  Complexity: linear in the
    // size of the set.
    template <class Element>
    static constexpr std::size_t index_of_ = [] consteval {
        std::size_t index = 0;
        for (const std::meta::info element : {^^Elems...}) {
            if (std::meta::dealias(element) == std::meta::dealias(^^Element)) return index;
            ++index;
        }
        return index;
    }();

    std::tuple<detail::hold_slot_t<Elems>...> slots_;
    bool live_ = true;
};

}  // namespace fixy::session

// ── Armed cells ──────────────────────────────────────────────────────

namespace fixy::session::detail::delegation_armed_witness {
struct Wire {};
struct NamesItsProtocol {
    using protocol = End;
    using resource_type = Wire;
};
struct HoldsEndpoint {
    int sequence = 0;
    NamesItsProtocol endpoint;
};
struct PlainMessage {
    int sequence = 0;
};
}  // namespace fixy::session::detail::delegation_armed_witness

template <>
struct foundation::contracts::armed_cell<::fixy::session::detail::payload_conveys_delegation> {
    using accepts = witnesses<
        ::fixy::session::DelegatedSession<::fixy::session::End, ::fixy::session::detail::delegation_armed_witness::Wire,
                                          void, ::foundation::permissions::EmptyPermSet>,
        ::fixy::session::detail::delegation_armed_witness::HoldsEndpoint,
        ::fixy::session::detail::delegation_armed_witness::NamesItsProtocol*, void*>;
    using refuses =
        witnesses<int, ::fixy::session::detail::delegation_armed_witness::PlainMessage,
                  ::fixy::session::Send<int, ::fixy::session::End>,
                  ::fixy::session::VendorPinned<::fixy::session::VendorBackend::Portable, ::fixy::session::End>>;
};
