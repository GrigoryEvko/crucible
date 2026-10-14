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
// The four markers that carry a token or a loan are linear
// (foundation/Linear.h).  A receive is their door: the handle takes the
// token or the loan out of the message into its set.
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
// fixy/session/Classification.h holds the walk that reads a payload, and
// the facts and the gates of one payload.  This header reads those facts.

#include <foundation/Brand.h>
#include <foundation/Linear.h>
#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Armed.h>
#include <foundation/contracts/Pre.h>
#include <foundation/permissions/PermSet.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>
#include <foundation/permissions/SharedPermission.h>
#include <fixy/session/Classification.h>
#include <fixy/session/Fold.h>
#include <fixy/session/Protocol.h>
#include <fixy/session/Registry.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <meta>
#include <tuple>
#include <type_traits>
#include <utility>

namespace fixy::session {

// The door that does each transition of a PermHold, defined below.  It is
// the one friend of the hold and of the loan markers.
class HoldFactory;

// ── The markers ─────────────────────────────────────────────────────
//
// Every marker is move-only.  A copy of a token is a second owner, and
// a copy of a read proof is a second loan that no set records.

template <class T, class Tag>
struct [[nodiscard]][[= ::foundation::linear::linear{}]] Transferable {
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
struct [[nodiscard]][[= ::foundation::linear::linear{}]] Returned {
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
struct [[nodiscard]][[= ::foundation::linear::linear{}]] Borrowed {
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
struct [[nodiscard]][[= ::foundation::linear::linear{}]] Released {
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
// None.  The head of fixy/session/Classification.h states which
// components delegate.  This value is the view for a reader.  No verdict
// reads it.
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
    namespace tr = ::fixy::session::transition;
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
    namespace tr = ::fixy::session::transition;
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
    namespace tr = ::fixy::session::transition;
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

// The gate that each mint of fixy/session/Mint.h reads.
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
// gate of fixy/session/Admission.h reads the set below for that check.
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
    namespace tr = ::fixy::session::transition;
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
    return ::fixy::session::transition::unregistered_message(unregistered_prefix, verdict.unregistered_node);
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
// is the spelling that the context gate of fixy/session/Admission.h reads.
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
