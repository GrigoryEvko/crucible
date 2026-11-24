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
//   Transferable<T, Tag, Brand>
//                            The sender gives the token to the recipient.
//   Returned<T, Tag, Brand>  The same move.  The type records that it
//                            closes an exclusive loan.
//   Borrowed<T, Tag, Brand>  A read loan.  The sender keeps the token
//                            parked and cannot use it until the loan
//                            ends.  The recipient gets the ReadLoan.
//   Released<T, Tag, Brand>  The end of a read loan.  It carries the
//                            ReadLoan back, and the lender gets its token
//                            back only with that loan.
//   DelegatedSession<P, R, Pol, PS>
//                            A live endpoint of another session, at
//                            protocol P over Resource R
//                            (fixy/session/Delegate.h).  The tokens in PS
//                            move with it.
//   SharedReader<Tag, Brand> A read share of a pool.  No set changes,
//                            because the pool counts its shares.
//   PeerMsg<Q, L, U>, Labelled<L, U>
//                            A keyed message.  Its payload U changes the
//                            sets as a member of the message does.  The
//                            value step of the handle moves U after the
//                            label word (fixy/session/Handle.h).
//
// A bare Permission<Tag, Brand> in a payload moves as a Transferable does.
//
// ── The brand ───────────────────────────────────────────────────────
//
// Each marker names the brand of its token, and so does each element of a
// held set (fixy/session/Classification.h).  The protocol is one type for
// the two ends of a session, so the receiver knows the brand of each token
// that it gets from the protocol alone.  A token keeps its brand in the
// slots and in each message, and it leaves the session with that brand.
// So no token leaves a session on the erased identity unless it entered on
// it, as a singleton root (foundation/permissions/Permission.h).
//
// A protocol that moves the token of a root names the brand of that root:
// it names brand_of_t of foundation/Brand.h after the root exists, or it is
// a template over the brand.  A marker whose brand is not the brand of the
// token in the set of the sender is refused, as a marker of a tag that the
// set does not hold is.
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
// set records this.  A send of Borrowed<T, Tag, Brand> takes the token
// Permission<Tag, Brand> out of the sender's set and puts LentOut<Tag, Brand>
// in its place, and the recipient's set takes BorrowedIn<Tag, Brand>.  A send
// of Released<T, Tag, Brand> reverses both.  While LentOut<Tag, Brand> is in
// the set, no send can move the token or lend it again, because the token is
// not in the set.  That is the rule of Saffrich,
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
#include <foundation/core/Index.h>
#include <foundation/core/Record.h>
#include <foundation/permissions/PermSet.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>
#include <foundation/permissions/SharedPermission.h>
#include <fixy/session/Classification.h>
#include <fixy/session/Fold.h>
#include <fixy/session/Protocol.h>
#include <fixy/session/Registry.h>

#include <cstddef>
#include <cstdint>
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

// The payload and the token of a marker are private, and so is the
// constructor of each marker.  Only the door of a hold builds a marker, from
// the token or the loan that the hold keeps, and only that door takes the
// payload and the token out of the marker together.  A session handle sends
// a marker that the door builds from the slots of the handle, so no token
// from outside the slots enters a message.  A transport reads the payload
// through peek() to write it to the wire.  peek() of a temporary marker is
// deleted, because the reference dangles at the end of the full expression.
template <class T, class Tag, class Brand>
struct [[nodiscard]][[= ::foundation::linear::linear{}]] Transferable {
    using payload_type = T;
    using transferred_perm = Tag;
    using brand_type = Brand;

    Transferable(const Transferable&) = delete("Transferable carries a linear token. A copy is a second owner");
    Transferable&
    operator=(const Transferable&) = delete("Transferable carries a linear token. A copy is a second owner");
    // The move of the token is trivial and its move assignment is
    // user-provided.  So GCC 16 counts the marker as not trivially copyable,
    // and the ABI passes it in registers when the move of the payload is
    // trivial.  An assignment would drop the token of the target.  The check
    // file of this header asserts the three facts.
    constexpr Transferable(Transferable&&) noexcept = default;
    Transferable& operator=(Transferable&&) =
        delete("Transferable carries a linear token, and an assignment would drop the token of the target");
    ~Transferable() = default;

    [[nodiscard]] constexpr T const& peek() const& noexcept { return value_; }
    T const& peek() const&& = delete("a reference into a temporary marker dangles at the end of the full expression");

private:
    friend class HoldFactory;

    // The message holds the token of the slots, with the brand that the
    // protocol names.
    constexpr Transferable(T v, ::foundation::permissions::Permission<Tag, Brand>&& p) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : value_{std::move(v)}, perm_{std::move(p)} {}

    T value_;
    [[no_unique_address]] ::foundation::permissions::Permission<Tag, Brand> perm_;
};

template <class T, class Tag, class Brand>
struct [[nodiscard]][[= ::foundation::linear::linear{}]] Returned {
    using payload_type = T;
    using returned_perm = Tag;
    using brand_type = Brand;

    Returned(const Returned&) = delete("Returned carries a linear token. A copy is a second owner");
    Returned& operator=(const Returned&) = delete("Returned carries a linear token. A copy is a second owner");
    // The move has the shape of the move of Transferable.
    constexpr Returned(Returned&&) noexcept = default;
    Returned& operator=(Returned&&) =
        delete("Returned carries a linear token, and an assignment would drop the token of the target");
    ~Returned() = default;

    [[nodiscard]] constexpr T const& peek() const& noexcept { return value_; }
    T const& peek() const&& = delete("a reference into a temporary marker dangles at the end of the full expression");

private:
    friend class HoldFactory;

    // The token comes from the slots, as it does for Transferable.
    constexpr Returned(T v, ::foundation::permissions::Permission<Tag, Brand>&& p) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : value_{std::move(v)}, perm_{std::move(p)} {}

    T value_;
    [[no_unique_address]] ::foundation::permissions::Permission<Tag, Brand> perm_;
};

// The loan comes from mint_read_loan, which parks the token of the tag.
// There is no constructor that takes no loan, so a Borrowed cannot exist
// without a parked token.  The loan is private: the recipient reaches it
// only through accept_loan, and reads only through the door of its hold.
template <class T, class Tag, class Brand>
struct [[nodiscard]][[= ::foundation::linear::linear{}]] Borrowed {
    using payload_type = T;
    using borrowed_perm = Tag;
    using brand_type = Brand;

    Borrowed(const Borrowed&) = delete("Borrowed is one read loan. A copy is a second loan that no set records");
    Borrowed&
    operator=(const Borrowed&) = delete("Borrowed is one read loan. A copy is a second loan that no set records");
    // The move of the loan is trivial and its move assignment is
    // user-provided.  So GCC 16 counts the marker as not trivially
    // copyable, and the ABI passes it in registers.  The check file of this
    // header asserts the two facts.
    constexpr Borrowed(Borrowed&&) noexcept = default;
    Borrowed& operator=(Borrowed&&) = delete("Borrowed holds one read loan for its whole life");
    ~Borrowed() = default;

    [[nodiscard]] constexpr T const& peek() const& noexcept { return value_; }
    T const& peek() const&& = delete("a reference into a temporary marker dangles at the end of the full expression");

private:
    friend class HoldFactory;

    // The loan has the brand of the token that the lender parks.
    constexpr Borrowed(T v, ::foundation::permissions::ReadLoan<Tag, Brand>&& loan) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : value_{std::move(v)}, loan_{std::move(loan)} {}

    T value_;
    [[no_unique_address]] ::foundation::permissions::ReadLoan<Tag, Brand> loan_;
};

// The end of a read loan carries the loan back.  The lender gets its
// token only from the parked token and this loan together, so a release
// that no borrower sent cannot reopen the region.  The type-level check
// makes the send sound too: the sender must hold BorrowedIn<Tag, Brand>.
template <class T, class Tag, class Brand>
struct [[nodiscard]][[= ::foundation::linear::linear{}]] Released {
    using payload_type = T;
    using released_perm = Tag;
    using brand_type = Brand;

    Released(const Released&) = delete("Released ends one read loan. A copy ends it a second time");
    Released& operator=(const Released&) = delete("Released ends one read loan. A copy ends it a second time");
    // The move has the shape of the move of Borrowed.  An assignment would
    // write over the loan of the target, and the move assignment of a loan
    // does nothing.  So the loan of the target would leave through no door,
    // and its lender would wait for it with no end.
    constexpr Released(Released&&) noexcept = default;
    Released& operator=(Released&&) =
        delete("Released carries one read loan back, and an assignment would drop the loan of the target");
    ~Released() = default;

    [[nodiscard]] constexpr T const& peek() const& noexcept { return value_; }
    T const& peek() const&& = delete("a reference into a temporary marker dangles at the end of the full expression");

private:
    friend class HoldFactory;

    // The loan has the brand of the loan of Borrowed.
    constexpr Released(T v, ::foundation::permissions::ReadLoan<Tag, Brand>&& loan) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : value_{std::move(v)}, loan_{std::move(loan)} {}

    T value_;
    [[no_unique_address]] ::foundation::permissions::ReadLoan<Tag, Brand> loan_;
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
    // value gives a Record of that value and the reader.
    template <class Body>
        requires ::foundation::permissions::ReadViewBody<Body, Tag, Brand>
              && ::foundation::linear::HoldsNoLinearValue<Body>
    [[nodiscard]] auto
    read(Body&& body) && noexcept(::foundation::permissions::detail::read_view_door_nothrow_v<
                                  ::foundation::permissions::SharedPermissionGuard<Tag, Brand>, Body>) {
        auto lent = ::foundation::permissions::with_read_view(std::move(guard_), std::forward<Body>(body));
        if constexpr (std::is_same_v<decltype(lent), ::foundation::permissions::SharedPermissionGuard<Tag, Brand>>) {
            return SharedReader{std::move(lent)};
        } else {
            auto [value, guard] = std::move(lent);
            return ::foundation::core::Record{std::move(value), SharedReader{std::move(guard)}};
        }
    }

    // A body that holds a linear value by value binds to this overload, so
    // the refusal gives its reason (HoldsNoLinearValue of foundation/Linear.h).
    template <class Body>
        requires(!::foundation::linear::HoldsNoLinearValue<Body>)
                 && ::foundation::permissions::ReadViewBody<Body, Tag, Brand>
    void read(Body&& body) && = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

    [[nodiscard]] bool holds_share() const noexcept { return guard_.holds_share(); }

private:
    ::foundation::permissions::SharedPermissionGuard<Tag, Brand> guard_;
};

// The payload rules of the markers (fixy/session/Registry.h).  The payload
// walk of fixy/session/Classification.h reads the tag of a marker at
// argument 1, its brand at argument 2 and its value at argument 0.  A share
// changes no set, and its brand is at argument 1, so the stable hash of a
// share erases its brand as it erases the brand of a marker.  The row of a
// marker is the row of its value, and a share carries no row.
namespace combinators::perm {

inline constexpr ::fixy::session::transition::payload_rule transferable{
    .shape = ^^Transferable,
    .role = ::fixy::session::transition::payload_role::moves,
    .tag_argument = 1,
    .brand_argument = 2,
    .value_argument = 0,
    .row_arguments = std::uint64_t{1}};

inline constexpr ::fixy::session::transition::payload_rule returned{
    .shape = ^^Returned,
    .role = ::fixy::session::transition::payload_role::moves,
    .tag_argument = 1,
    .brand_argument = 2,
    .value_argument = 0,
    .row_arguments = std::uint64_t{1}};

inline constexpr ::fixy::session::transition::payload_rule borrowed{
    .shape = ^^Borrowed,
    .role = ::fixy::session::transition::payload_role::lends,
    .tag_argument = 1,
    .brand_argument = 2,
    .value_argument = 0,
    .row_arguments = std::uint64_t{1}};

inline constexpr ::fixy::session::transition::payload_rule released{
    .shape = ^^Released,
    .role = ::fixy::session::transition::payload_role::releases,
    .tag_argument = 1,
    .brand_argument = 2,
    .value_argument = 0,
    .row_arguments = std::uint64_t{1}};

inline constexpr ::fixy::session::transition::payload_rule shared_reader{
    .shape = ^^SharedReader, .role = ::fixy::session::transition::payload_role::share, .brand_argument = 1};

inline constexpr ::fixy::session::transition::seal payload_rule_seal{
    .kind = ^^::fixy::session::transition::payload_rule, .count = 5};

}  // namespace combinators::perm

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

// ── The protocol walks ──────────────────────────────────────────────
//
// Each walk below is an algebra of the registry fold of
// fixy/session/Fold.h.  It reads the spine of a protocol through the
// registry of fixy/session/Protocol.h: the next node of each node, and each
// branch of a choice.  A payload is not a node, so a walk does not enter it,
// and each walk reads a payload through payload_facts_of, so a payload has
// one walk.  A node that the registry does not know is not entered.  The
// fold keeps one answer for each node and walk in a translation unit, so a
// node that many protocols share costs one read.  A walk that lists items
// keeps the first place of each item in a walk that reads a node first, then
// its continuation, then each branch.
//
// A fold does not end on a cycle of names.  So each entry point below
// refuses a protocol with a named state that reaches itself before it folds,
// as each entry check of fixy/session/Protocol.h does.

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

namespace detail::walks {

namespace tr = ::fixy::session::transition;

// The roles that a protocol names as peers: the peer of each keyed message
// that a step carries, and the role of the note of each choice.  A node
// that the registry does not know names no peer.
struct named_peers_algebra {
    using result = tr::static_run<std::meta::info>;
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
        const payload_lookup rule = message_rule_of(view.payload);
        const result below = child(view.next, ctx);
        if (!rule.is_found || rule.entry.peer_argument == tr::npos) return below;
        return tr::join_runs(tr::run_of_one(rule_argument_of(view.payload, rule.entry.peer_argument)), below);
    }
    template <class Child>
    consteval result choice(const tr::node& view, context ctx, const Child& child) const {
        result roles = view.note_role == std::meta::info{} ? result{} : tr::run_of_one(view.note_role);
        for (const std::meta::info branch : view.branches)
            roles = tr::join_runs(roles, child(branch, ctx));
        return roles;
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
};

// The roles that `protocol` names as peers, each one time.  Complexity: one
// fold, linear in the distinct nodes, times the joins of the runs of roles.
[[nodiscard]] consteval tr::static_run<std::meta::info> named_peers_of(std::meta::info protocol) {
    require_acyclic_names(protocol);
    return tr::fold(protocol_registry, protocol, named_peers_algebra{}, 0);
}

// True when the send `step` hands an endpoint of a session to the receiver
// that its keyed message names, and the protocol of that session names the
// receiver as a peer.
[[nodiscard]] consteval bool hands_to_own_peer(const tr::node& step) {
    const payload_lookup rule = message_rule_of(step.payload);
    if (!rule.is_found || rule.entry.peer_argument == tr::npos) return false;
    const std::meta::info receiver = rule_argument_of(step.payload, rule.entry.peer_argument);
    for (const std::meta::info carried :
         payload_facts_of(rule_argument_of(step.payload, rule.entry.value_argument)).hand_offs()) {
        if (tr::run_holds(named_peers_of(hand_off_protocol_of(carried)), receiver)) return true;
    }
    return false;
}

// True when a send of the protocol hands an endpoint of a session to a
// role that the protocol of that session names as a peer.
struct own_peer_delegation_algebra {
    using result = bool;
    using context = int;

    template <class Child>
    consteval bool terminal(const tr::node&, context, const Child&) const {
        return false;
    }
    template <class Child>
    consteval bool back(const tr::node&, context, const Child&) const {
        return false;
    }
    template <class Child>
    consteval bool step(const tr::node& view, context ctx, const Child& child) const {
        if (view.entry.direction == tr::polarity::output && hands_to_own_peer(view)) return true;
        return child(view.next, ctx);
    }
    template <class Child>
    consteval bool choice(const tr::node& view, context ctx, const Child& child) const {
        for (const std::meta::info branch : view.branches) {
            if (child(branch, ctx)) return true;
        }
        return false;
    }
    template <class Child>
    consteval bool binder(const tr::node& view, context ctx, const Child& child) const {
        return child(view.next, ctx);
    }
    template <class Child>
    consteval bool wrapper(const tr::node& view, context ctx, const Child& child) const {
        return child(view.next, ctx);
    }
    template <class Child>
    consteval bool marker(const tr::node& view, context ctx, const Child& child) const {
        return child(view.next, ctx);
    }
    template <class Child>
    consteval bool named(const tr::node& view, context ctx, const Child& child) const {
        return child(view.next, ctx);
    }
    consteval bool unregistered(const tr::node&, context) const { return false; }
};

}  // namespace detail::walks

namespace detail {

// Adds to `peers` each role that a protocol names as a peer and that
// `peers` does not hold: the peer of each PeerMsg that a step carries, and
// the role of the note of each choice.  Complexity: the fold of
// named_peers_of, then the scan of `peers` for each role.
consteval void collect_named_peers(std::meta::info protocol, stack<std::meta::info>& peers) {
    for (const std::meta::info role : walks::named_peers_of(protocol)) {
        if (!holds_type(peers, role)) push(peers, role);
    }
}

// True when a Send of the protocol hands an endpoint of a session to a
// role that the protocol of that session names as a peer.  Complexity: one
// fold, linear in the distinct nodes, with the peer walk of each delegated
// protocol.
[[nodiscard]] consteval bool delegates_to_own_peer(std::meta::info protocol) {
    require_acyclic_names(protocol);
    return walks::tr::fold(protocol_registry, protocol, walks::own_peer_delegation_algebra{}, 0);
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

[[nodiscard]] consteval bool delivery_is_refused(const DeliveryVerdict& verdict) noexcept {
    return verdict.unregistered_node != std::meta::info{} || verdict.payload.refusal != PayloadRefusal::None;
}

// One item of the walk of one protocol: a region that a receive delivers,
// or the protocol of an endpoint that a receive hands over, whose regions
// the receiver gets too.
enum class DeliveryItemKind : std::uint8_t {
    region,
    hand_off
};

struct DeliveryItem {
    std::meta::info type{};
    DeliveryItemKind kind = DeliveryItemKind::region;

    constexpr bool operator==(const DeliveryItem&) const = default;
};

// The walk of one protocol, with no received endpoint entered: its items,
// each one time, and the first refusal, which stands after the last item.
struct DeliveryRun {
    DeliveryVerdict verdict{};
    ::fixy::session::transition::static_run<DeliveryItem> items{};
};

}  // namespace detail

namespace detail::walks {

// The items of each Recv of a protocol, on each branch and in each loop
// body.  A node that the registry does not know, and a payload that the
// payload walk refuses, stop the walk.  The fold does not enter the protocol
// of a received endpoint, so a protocol whose endpoint comes back to a node
// of the walk ends.  walk_delivered_regions below enters each such protocol
// one time.
struct delivery_algebra {
    using result = DeliveryRun;
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
        if (view.entry.direction != tr::polarity::input) return child(view.next, ctx);
        const PayloadFacts facts = payload_facts_of(view.payload);
        if (facts.refusal != PayloadRefusal::None) {
            return {DeliveryVerdict{{}, PayloadVerdict{facts.refusal, facts.refused_type}}, {}};
        }
        const result below = child(view.next, ctx);
        return {below.verdict, tr::join_runs(items_of(facts), below.items)};
    }
    template <class Child>
    consteval result choice(const tr::node& view, context ctx, const Child& child) const {
        result answer{};
        for (const std::meta::info branch : view.branches) {
            const result below = child(branch, ctx);
            answer.items = tr::join_runs(answer.items, below.items);
            if (delivery_is_refused(below.verdict)) {
                answer.verdict = below.verdict;
                return answer;
            }
        }
        return answer;
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
    consteval result unregistered(const tr::node& view, context) const { return {DeliveryVerdict{view.type, {}}, {}}; }

private:
    static consteval void add_item(tr::stack<DeliveryItem>& items, const DeliveryItem& item) {
        for (const DeliveryItem& held : items) {
            if (held == item) return;
        }
        tr::push(items, item);
    }

    // The items of one received payload: each region that it moves, lends
    // or releases, each region of the set of each endpoint that it hands
    // over, and the protocol of each such endpoint.
    static consteval tr::static_run<DeliveryItem> items_of(const PayloadFacts& facts) {
        tr::stack<DeliveryItem> items{};
        // The region of each element is its tag, so the delivered set is a
        // set of regions.
        const std::vector<std::meta::info> elements = std::meta::template_arguments_of(facts.receiver_gains);
        const std::meta::info* const element = elements.data();
        const std::size_t element_count = elements.size();
        for (std::size_t place = 0; place < element_count; ++place)
            add_item(items, DeliveryItem{region_of(element[place]), DeliveryItemKind::region});
        // A received endpoint holds the tokens of its set in its slots, so
        // the receiver holds those regions too.
        for (const std::meta::info handed : tr::template_arguments_run(facts.hand_off_tags))
            add_item(items, DeliveryItem{region_of(handed), DeliveryItemKind::region});
        for (const std::meta::info carried : facts.hand_offs())
            add_item(items, DeliveryItem{hand_off_protocol_of(carried), DeliveryItemKind::hand_off});
        if (items.top == 0) return {};
        return tr::to_static_run(items);
    }
};

}  // namespace detail::walks

namespace detail {

// The regions and the protocols of the received endpoints that the walk
// entered, and why it stopped.
struct DeliveryWalk {
    stack<std::meta::info> regions;
    stack<std::meta::info> entered;
    DeliveryVerdict verdict;
};

// Adds the regions of `protocol` to the walk, in the order of its items, and
// enters the protocol of each endpoint that it receives, at the place of
// that endpoint.  A protocol that the walk entered adds nothing again, so the
// walk ends when an endpoint comes back to a protocol of the walk.  A node
// gives the same regions on every path that reaches it, so this order is
// the order of a walk that visits each node one time, also across the
// delegated protocols.
consteval void walk_delivered_regions(std::meta::info protocol, DeliveryWalk& walk) {
    require_acyclic_names(protocol);
    const DeliveryRun run = walks::tr::fold(protocol_registry, protocol, walks::delivery_algebra{}, 0);
    for (const DeliveryItem& item : run.items) {
        if (item.kind == DeliveryItemKind::region) {
            if (!holds_type(walk.regions, item.type)) push(walk.regions, item.type);
            continue;
        }
        if (holds_type(walk.entered, item.type)) continue;
        push(walk.entered, item.type);
        walk_delivered_regions(item.type, walk);
        if (delivery_is_refused(walk.verdict)) return;
    }
    walk.verdict = run.verdict;
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
    push(walk.entered, std::meta::dealias(protocol));
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

// The result of the walk over one protocol.  A walk that stops the constant
// evaluation of the cell with an error, such as the refusal of a cycle of
// names in the protocol of a received endpoint, makes the substitution fail,
// and the failure of a substitution names no cause.  The walk then runs again
// here, in the evaluation that asked, so its error stops that evaluation
// with its own text.  Complexity: one walk for each protocol type in a
// translation unit, and a lookup after that.  A walk that fails runs two
// times.
[[nodiscard]] consteval DeliveryFacts delivery_facts_of(std::meta::info protocol) {
    if (!std::meta::can_substitute(^^delivery_facts_cached, {protocol})) return delivery_facts(protocol);
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

// ── The slot form of a payload ──────────────────────────────────────
//
// A session handle keeps each token of its set, the parked token of each
// LentOut and the loan of each BorrowedIn, with their brands, in its slots
// (HoldSlots below).  A message that moves a token or a loan takes
// it out of the slots of the sender and puts it into the slots of the
// receiver.  So the set of a handle and its slots are one ledger.  The step
// builds the message from the slots, and the caller gives the value only,
// so no token from outside the slots enters a message.  A step can do that
// only when the payload is the marker or the token itself:
//
//   Plain     the payload moves, lends and releases nothing
//   Move      a Transferable or a Returned whose value is plain
//   Lend      a Borrowed whose value is plain
//   Release   a Released whose value is plain
//   Token     a bare Permission<Tag, Brand>
//   Outside   a token or a loan in a member of the payload, or in the value
//             of a marker.  No step can take it out of the slots, so a send
//             and a receive refuse it.
//
// A keyed message, a PeerMsg or a Labelled, has the form of its payload.  A
// hand-off keeps the tokens of its endpoint in the slots of the endpoint, so
// it moves nothing (the section on delegation of
// fixy/session/Classification.h).

enum class SlotForm : std::uint8_t {
    Plain,
    Move,
    Lend,
    Release,
    Token,
    Outside,
};

namespace detail {

[[nodiscard]] consteval bool is_empty_set(std::meta::info set) {
    return ::fixy::session::transition::template_arguments_run(std::meta::dealias(set)).empty();
}

// True when the payload of the facts moves, lends and releases nothing.
[[nodiscard]] consteval bool moves_nothing(const PayloadFacts& facts) {
    return facts.refusal == PayloadRefusal::None && is_empty_set(facts.sender_requires)
        && is_empty_set(facts.sender_gains) && is_empty_set(facts.receiver_requires)
        && is_empty_set(facts.receiver_gains);
}

// The slot form of one payload.  Complexity: one or two lookups of the
// cached payload facts.
[[nodiscard]] consteval SlotForm slot_form_of(std::meta::info payload) {
    namespace tr = ::fixy::session::transition;
    if (moves_nothing(payload_facts_of(payload))) return SlotForm::Plain;
    std::meta::info message = std::meta::dealias(payload);
    const payload_lookup keyed = message_rule_of(message);
    if (keyed.is_found) message = rule_argument_of(message, keyed.entry.value_argument);
    if (!std::meta::has_template_arguments(message)) return SlotForm::Outside;
    const tr::static_run<std::meta::info> argument = tr::template_arguments_run(message);
    const bool is_value_plain = moves_nothing(payload_facts_of(argument[0]));
    if (payload_family_is(message, ^^Transferable) || payload_family_is(message, ^^Returned)) {
        return is_value_plain ? SlotForm::Move : SlotForm::Outside;
    }
    if (payload_family_is(message, ^^Borrowed)) return is_value_plain ? SlotForm::Lend : SlotForm::Outside;
    if (payload_family_is(message, ^^Released)) return is_value_plain ? SlotForm::Release : SlotForm::Outside;
    if (payload_family_is(message, ^^::foundation::permissions::Permission)) return SlotForm::Token;
    return SlotForm::Outside;
}

// The tags that the endpoints of the hand-offs of a payload hold, as a
// PermSet.
[[nodiscard]] consteval std::meta::info hand_off_tags_of(std::meta::info payload) {
    return payload_facts_of(payload).hand_off_tags;
}

}  // namespace detail

template <class P>
inline constexpr SlotForm slot_form_v = detail::slot_form_of(^^P);

// A sender can send a payload when the walk accepts it, the sender holds
// what the payload takes, no gained loan state names a region the sender
// still holds in another state, a step can build the payload from the
// slots, and no endpoint that the payload hands off holds a region of the
// set of the sender.  One region in the set of a handle and in the set of an
// endpoint that it holds is two claims on that region.
template <class P, class PS>
concept SendablePayload =
    is_permission_classified_v<P> && ::foundation::permissions::perm_set_subset(^^detail::sender_requires_t<P>, ^^PS)
    && detail::regions_disjoint(^^::foundation::permissions::perm_set_difference_t<PS, detail::sender_requires_t<P>>,
                                ^^detail::sender_gains_t<P>)
    && slot_form_v<P> != SlotForm::Outside && detail::regions_disjoint(^^PS, detail::hand_off_tags_of(^^P));

// A recipient can receive a payload when the walk accepts it, the
// recipient holds what the payload closes, nothing it gains names a region
// the recipient already holds in any state, a step can put the payload into
// the slots, and no endpoint that the payload hands off holds a region of
// the set of the recipient.  Two tokens of one region in one set are two
// owners of it, and so are a token of the set and a token in the slots of a
// received endpoint.
template <class P, class PS>
concept ReceivablePayload =
    is_permission_classified_v<P> && ::foundation::permissions::perm_set_subset(^^detail::receiver_requires_t<P>, ^^PS)
    && detail::regions_disjoint(^^::foundation::permissions::perm_set_difference_t<PS, detail::receiver_requires_t<P>>,
                                ^^detail::receiver_gains_t<P>)
    && slot_form_v<P> != SlotForm::Outside && detail::regions_disjoint(^^PS, detail::hand_off_tags_of(^^P));

// True when the set holds an open loan: a LentOut or a BorrowedIn.  A
// handle must not close while one is open.
template <class PS>
concept perm_set_has_open_loan_v = detail::holds_open_loan(^^PS);

// ── The tokens of a set, held ───────────────────────────────────────
//
// A PermHold keeps the token object for each element of its set: the
// Permission<Tag, Brand> of each token, the LentPermission<Tag, Brand> of
// each LentOut<Tag, Brand>, and the ReadLoan<Tag, Brand> of each
// BorrowedIn<Tag, Brand>.  Every transition consumes the hold and returns
// the next one, whose set is the set after the step.  So the tokens and the
// set cannot disagree:
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

// The object that a hold keeps for one element of its set.  An element that
// is no token, no parked token and no loan names no object, and it stops the
// build: a bare tag names a region and no brand.
[[nodiscard]] consteval std::meta::info hold_slot_of(std::meta::info element) {
    const std::meta::info bare = std::meta::dealias(element);
    if (payload_family_is(bare, ^^LentOut)) {
        return std::meta::substitute(^^::foundation::permissions::LentPermission,
                                     std::meta::template_arguments_of(bare));
    }
    if (payload_family_is(bare, ^^BorrowedIn)) {
        return std::meta::substitute(^^::foundation::permissions::ReadLoan, std::meta::template_arguments_of(bare));
    }
    if (payload_family_is(bare, ^^::foundation::permissions::Permission)) return bare;
    throw std::meta::exception(u8"fixy::session::diagnostic [PermissionImbalance]: an element of a held permission "
                               u8"set is no token form.  A held set names Permission<Tag, Brand>, "
                               u8"LentOut<Tag, Brand> or BorrowedIn<Tag, Brand>, so that each slot keeps the brand "
                               u8"of its token.  A bare tag names a region and no brand.",
                               bare);
}

template <class Element>
using hold_slot_t = [:hold_slot_of(^^Element):];

// True when the tag of a token is a plain tag, and not a loan state.
template <class Tag>
concept PlainTag = !payload_family_is(std::meta::dealias(^^Tag), ^^LentOut)
                && !payload_family_is(std::meta::dealias(^^Tag), ^^BorrowedIn);

// The element of the set whose region is the tag, or a null reflection.  A
// set holds one element for each region.  Complexity: linear in the size of
// the set.
[[nodiscard]] consteval std::meta::info element_of_region(std::meta::info set, std::meta::info tag) {
    const std::meta::info wanted = std::meta::dealias(tag);
    for (const std::meta::info element : ::fixy::session::transition::template_arguments_run(std::meta::dealias(set))) {
        if (region_of(element) == wanted) return std::meta::dealias(element);
    }
    return {};
}

// True when the set holds an element of the family `family` for the region
// of the tag: a token, a LentOut or a BorrowedIn.
[[nodiscard]] consteval bool holds_element_of(std::meta::info set, std::meta::info tag, std::meta::info family) {
    const std::meta::info element = element_of_region(set, tag);
    return element != std::meta::info{} && payload_family_is(element, family);
}

[[nodiscard]] consteval bool holds_token_of(std::meta::info set, std::meta::info tag) {
    return holds_element_of(set, tag, ^^::foundation::permissions::Permission);
}

[[nodiscard]] consteval bool holds_loan_of(std::meta::info set, std::meta::info tag) {
    return holds_element_of(set, tag, ^^BorrowedIn);
}

// The element of the set for the region of the tag, and its brand.  Each
// spelling stands after a gate that the set holds that element.
template <class Set, class Tag>
using element_of_t = [:element_of_region(^^Set, ^^Tag):];

template <class Set, class Tag>
using element_brand_t = [:std::meta::template_arguments_of(element_of_region(^^Set, ^^Tag))[1]:];

// True when the message moves its token to the recipient: a Transferable
// or a Returned, received as an rvalue.
template <class Message>
concept MovesAToken = payload_family_is(^^Message, ^^Transferable) || payload_family_is(^^Message, ^^Returned);

// The gate of a read loan that a hold of Set lends as a Borrowed of T,
// and of a read loan that a hold of Set accepts.  The loan takes the brand
// of the token that the set holds for the tag.  HoldFactory and each member
// of PermHold read the same gate.
template <class Set, class Tag, class T>
concept HoldCanLend = holds_token_of(^^Set, ^^Tag) && SendablePayload<Borrowed<T, Tag, element_brand_t<Set, Tag>>, Set>
                   && ::foundation::permissions::ReadViewNeedsNoCtx<Tag>;

template <class Set, class T, class Tag, class Brand>
concept HoldCanAcceptLoan = ReceivablePayload<Borrowed<T, Tag, Brand>, Set>;

// The gate of a release that a hold of Set sends for the region of the tag.
// The release takes the brand of the loan that the set holds.
template <class Set, class Tag, class T>
concept HoldCanRelease =
    holds_loan_of(^^Set, ^^Tag) && SendablePayload<Released<T, Tag, element_brand_t<Set, Tag>>, Set>;

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

// The slots of a set: the object that hold_slot_of names for each element of
// the set, in the order of the set.  A PermHold keeps them, and so does a
// session handle.  Only HoldFactory builds slots that hold a token, and only
// HoldFactory reads them, so no other code reaches the tokens of a set.  The
// slots of the empty set hold nothing, and any code can build them.  The
// slots carry no flag of state: the PermHold around them, or the handle that
// keeps them, knows whether they are live.  A set of empty token types has
// empty slots, so a [[no_unique_address]] member of slots adds no byte.
template <class Set>
class HoldSlots;

template <class... Elems>
class HoldSlots<::foundation::permissions::PermSet<Elems...>> {
    friend class ::fixy::session::HoldFactory;

    using parts_type = ::foundation::core::Record<detail::hold_slot_t<Elems>...>;

    [[no_unique_address]] parts_type parts_;

    constexpr explicit HoldSlots(detail::hold_from_slots, parts_type&& parts) noexcept : parts_{std::move(parts)} {}

public:
    constexpr HoldSlots() noexcept
        requires(sizeof...(Elems) == 0)
    = default;

    HoldSlots(const HoldSlots&) = delete("the slots own the tokens of a set. A copy owns them twice");
    HoldSlots& operator=(const HoldSlots&) = delete("the slots own the tokens of a set. A copy owns them twice");
    constexpr HoldSlots(HoldSlots&&) noexcept = default;
    // A handle takes the position of another handle by a move assignment,
    // and it takes the slots of that handle with it.
    constexpr HoldSlots& operator=(HoldSlots&&) noexcept = default;
    ~HoldSlots() = default;
};

namespace detail {

// The position of an element in the set.  Complexity: linear in the size of
// the set.
template <class Element, class Set>
inline constexpr std::size_t slot_index_v = [] consteval {
    std::size_t index = 0;
    for (const std::meta::info element :
         ::fixy::session::transition::template_arguments_run(std::meta::dealias(^^Set))) {
        if (std::meta::dealias(element) == std::meta::dealias(^^Element)) return index;
        ++index;
    }
    return index;
}();

// The tag that a marker names, read from its template arguments.
template <class Message>
using marker_tag_t = [:std::meta::template_arguments_of(std::meta::dealias(^^Message))[1]:];

// The brand that a marker names.
template <class Message>
using marker_brand_t = [:std::meta::template_arguments_of(std::meta::dealias(^^Message))[2]:];

// True when a step builds the message from the slots of a set, or puts the
// message into them: a marker of the slot form Move, Lend or Release that
// the set can send or receive.
template <class Message>
concept IsSlotMarker = slot_form_v<Message> == SlotForm::Move || slot_form_v<Message> == SlotForm::Lend
                    || slot_form_v<Message> == SlotForm::Release;

template <class Message, class Set>
concept BuildsFromSlots = IsSlotMarker<Message> && SendablePayload<Message, Set>;

template <class Message, class Set>
concept PutsIntoSlots = IsSlotMarker<Message> && ReceivablePayload<Message, Set>;

// The gate of a pack of the token of the tag into a Transferable of T.  The
// marker takes the brand of the token that the set holds for the tag.
template <class Set, class Tag, class T>
concept HoldCanPack = PlainTag<Tag> && holds_token_of(^^Set, ^^Tag)
                   && BuildsFromSlots<Transferable<T, Tag, element_brand_t<Set, Tag>>, Set>;

}  // namespace detail

// The door that builds every hold and every marker.  Each public member
// takes live tokens, live slots or a live hold, and does one complete
// transition with the gate of that transition, so a direct call is no
// weaker than the member of the hold or the step of the handle that calls
// it.  A member that takes slots gives back the slots after the transition,
// and the slots that it took are spent.  No member gives out the slots of a
// handle: the handle keeps them in a private member.  The class is final,
// and no object of it exists.  foundation/NoObject.h tells why no byte route
// makes one.
class HoldFactory final : ::foundation::NoObject<HoldFactory> {
    struct no_incoming {};

    // The object that the slots keep for one element of their set.  A storage
    // door: the slot is a part of the HoldSlots that the step of the factory takes.
    template <class Element, class Set>
    [[= ::foundation::linear::moves_own_storage{}]] [[nodiscard]] static constexpr auto&
        slot_(HoldSlots<Set>& slots) noexcept {
        return slots.parts_.at(::foundation::core::index<detail::slot_index_v<Element, Set>>());
    }

    // The object of one element of the next set: the object that the slots
    // keep for it, or the incoming object when the element is new.  A
    // transition adds at most one element, so at most one element takes the
    // incoming object.
    template <class Element, class Set, class Incoming>
    [[nodiscard]] static constexpr detail::hold_slot_t<Element> pick_(HoldSlots<Set>& from,
                                                                      Incoming&& incoming) noexcept {
        if constexpr (::foundation::permissions::perm_set_contains(^^Set, ^^Element)) {
            return std::move(slot_<Element>(from));
        } else {
            return detail::hold_slot_t<Element>{std::forward<Incoming>(incoming)};
        }
    }

    // Spends the slots and builds the slots of the set Next from their
    // objects and the incoming object.  The set of the last parameter
    // spells Next, so its elements give the pack of the new parts.  The pack
    // has the name and the position of the pack of CtxFitsSessionFrom of
    // fixy/session/Admission.h, because GCC prints one name for two packs at
    // one position, and the diagnostics of the mints name that pack.
    template <class Next, class Set, class Incoming, class... Tokens>
    [[nodiscard]] static constexpr HoldSlots<Next>
    build_(HoldSlots<Set>&& from, Incoming&& incoming,
           std::type_identity<::foundation::permissions::PermSet<Tokens...>>) noexcept {
        return HoldSlots<Next>{detail::hold_from_slots{}, typename HoldSlots<Next>::parts_type{pick_<Tokens>(
                                                              from, std::forward<Incoming>(incoming))...}};
    }

    template <class Next, class Set, class Incoming = no_incoming>
    [[nodiscard]] static constexpr HoldSlots<Next> transition_(HoldSlots<Set>&& from,
                                                               Incoming&& incoming = {}) noexcept {
        return build_<Next>(std::move(from), std::forward<Incoming>(incoming), std::type_identity<Next>{});
    }

    // The slots of a live hold, which the hold gives up.  A hold that a move
    // or a transition consumed aborts here.
    template <class Set>
    [[nodiscard]] static constexpr HoldSlots<Set> spend_(PermHold<Set>&& hold) noexcept {
        hold.require_live_();
        hold.live_ = false;
        return std::move(hold.slots_);
    }

    template <class Set>
    [[nodiscard]] static constexpr PermHold<Set> hold_(HoldSlots<Set>&& slots) noexcept {
        return PermHold<Set>{detail::hold_from_slots{}, std::move(slots)};
    }

    // Spends the slots and gives the prefix values and then each token of
    // the slots, in the order of the set, in one Record.
    template <class... Elems, std::size_t... Places, class... Prefix>
    [[nodiscard]] static constexpr ::foundation::core::Record<Prefix..., detail::hold_slot_t<Elems>...>
    tokens_from_(HoldSlots<::foundation::permissions::PermSet<Elems...>>&& slots, std::index_sequence<Places...>,
                 Prefix... prefix) noexcept((std::is_nothrow_move_constructible_v<Prefix> && ...)) {
        return ::foundation::core::Record<Prefix..., detail::hold_slot_t<Elems>...>{
            std::move(prefix)..., std::move(slots.parts_.at(::foundation::core::index<Places>()))...};
    }

public:
    // ── The slots ──────────────────────────────────────────────────

    // Consumes the tokens and builds the slots of their set.  Each token is
    // an element of the set, with its brand, and one tag is one region.
    template <class... Tags, class... Brands>
        requires ::foundation::permissions::DistinctTags<Tags...>
    [[nodiscard]] static constexpr HoldSlots<
        ::foundation::permissions::PermSet<::foundation::permissions::Permission<Tags, Brands>...>>
    slots_of(::foundation::permissions::Permission<Tags, Brands>... tokens) noexcept {
        using Set = ::foundation::permissions::PermSet<::foundation::permissions::Permission<Tags, Brands>...>;
        return HoldSlots<Set>{detail::hold_from_slots{}, typename HoldSlots<Set>::parts_type{std::move(tokens)...}};
    }

    // The slots of a set under another spelling of the same set.  A step
    // of a payload that moves nothing gives its next handle the set that
    // the delta of the payload spells.
    template <class Next, class Set>
        requires(::foundation::permissions::perm_set_equal(^^Set, ^^Next))
    [[nodiscard]] static constexpr HoldSlots<Next> as_set(HoldSlots<Set>&& slots) noexcept {
        if constexpr (std::is_same_v<Set, Next>) {
            return std::move(slots);
        } else {
            return transition_<Next>(std::move(slots));
        }
    }

    // The token of the region of the tag leaves the slots, with its brand.
    template <class Tag, class Set>
        requires(detail::PlainTag<Tag> && detail::holds_token_of(^^Set, ^^Tag))
    [[nodiscard]] static constexpr auto take_slot(HoldSlots<Set>&& slots) noexcept -> ::foundation::core::Record<
        detail::element_of_t<Set, Tag>,
        HoldSlots<::foundation::permissions::perm_set_remove_t<Set, detail::element_of_t<Set, Tag>>>> {
        using Token = detail::element_of_t<Set, Tag>;
        Token token = std::move(slot_<Token>(slots));
        return {std::move(token),
                transition_<::foundation::permissions::perm_set_remove_t<Set, Token>>(std::move(slots))};
    }

    // A token enters the slots, with its brand.  The set must hold no element
    // of its region.
    template <class Set, class Tag, class Brand>
        requires(detail::PlainTag<Tag> && detail::regions_disjoint(^^Set, ^^::foundation::permissions::PermSet<Tag>))
    [[nodiscard]] static constexpr auto put_slot(HoldSlots<Set>&& slots,
                                                 ::foundation::permissions::Permission<Tag, Brand> token) noexcept
        -> HoldSlots<
            ::foundation::permissions::perm_set_insert_t<Set, ::foundation::permissions::Permission<Tag, Brand>>> {
        return transition_<
            ::foundation::permissions::perm_set_insert_t<Set, ::foundation::permissions::Permission<Tag, Brand>>>(
            std::move(slots), std::move(token));
    }

    // Builds the message of a send from the slots, around the value that
    // the sender gives.  A Transferable and a Returned take the token of
    // their tag.  A Borrowed parks that token in the LentOut slot, where no
    // take reaches it, and the loan leaves inside the message: only a
    // Released that carries the same loan back unparks the token.  A
    // Released takes the loan of the BorrowedIn slot.
    template <class Message, class Set>
        requires detail::BuildsFromSlots<Message, Set>
    [[nodiscard]] static constexpr auto message_from_slots(HoldSlots<Set>&& slots,
                                                           typename Message::payload_type value) noexcept
        -> ::foundation::core::Record<Message, HoldSlots<perm_set_after_send_t<Set, Message>>> {
        using Tag = detail::marker_tag_t<Message>;
        using Brand = detail::marker_brand_t<Message>;
        using Token = ::foundation::permissions::Permission<Tag, Brand>;
        using Next = perm_set_after_send_t<Set, Message>;
        if constexpr (slot_form_v<Message> == SlotForm::Move) {
            Message message{std::move(value), std::move(slot_<Token>(slots))};
            return {std::move(message), transition_<Next>(std::move(slots))};
        } else if constexpr (slot_form_v<Message> == SlotForm::Lend) {
            auto [loan, parked] = ::foundation::permissions::mint_read_loan(std::move(slot_<Token>(slots)));
            Message message{std::move(value), std::move(loan)};
            return {std::move(message), transition_<Next>(std::move(slots), std::move(parked))};
        } else {
            Message message{std::move(value), std::move(slot_<BorrowedIn<Tag, Brand>>(slots))};
            return {std::move(message), transition_<Next>(std::move(slots))};
        }
    }

    // Puts the token or the loan of a received message into the slots, and
    // gives the value.  A Transferable and a Returned give their token, a
    // Borrowed gives its loan into the BorrowedIn slot, and a Released gives
    // its loan back to the parked token of the LentOut slot, which gives the
    // token.
    template <class Message, class Set>
        requires detail::PutsIntoSlots<Message, Set>
    [[nodiscard]] static constexpr auto value_into_slots(HoldSlots<Set>&& slots, Message&& message) noexcept
        -> ::foundation::core::Record<typename Message::payload_type, HoldSlots<perm_set_after_recv_t<Set, Message>>> {
        using Tag = detail::marker_tag_t<Message>;
        using Brand = detail::marker_brand_t<Message>;
        using Next = perm_set_after_recv_t<Set, Message>;
        typename Message::payload_type value = std::move(message.value_);
        if constexpr (slot_form_v<Message> == SlotForm::Move) {
            return {std::move(value), transition_<Next>(std::move(slots), std::move(message.perm_))};
        } else if constexpr (slot_form_v<Message> == SlotForm::Lend) {
            return {std::move(value), transition_<Next>(std::move(slots), std::move(message.loan_))};
        } else {
            ::foundation::permissions::LentPermission<Tag, Brand>& parked = slot_<LentOut<Tag, Brand>>(slots);
            return {std::move(value),
                    transition_<Next>(std::move(slots), ::foundation::permissions::mint_permission_after_loan(
                                                            std::move(parked), std::move(message.loan_)))};
        }
    }

    // Runs the body with a read view of a region that the slots borrow, and
    // puts the loan back.  The view has the brand of the loan.  A body that
    // returns nothing gives nothing back.
    template <class Tag, class Set, class Body>
        requires(detail::holds_loan_of(^^Set, ^^Tag))
             && ::foundation::permissions::ReadViewBody<Body, Tag, detail::element_brand_t<Set, Tag>>
             && ::foundation::linear::HoldsNoLinearValue<Body>
    [[nodiscard]] static constexpr auto read_slot(HoldSlots<Set>& slots, Body&& body) noexcept(
        ::foundation::permissions::detail::read_view_door_nothrow_v<detail::hold_slot_t<detail::element_of_t<Set, Tag>>,
                                                                    Body>) {
        using Loan = detail::element_of_t<Set, Tag>;
        detail::hold_slot_t<Loan>& slot = slot_<Loan>(slots);
        auto lent = ::foundation::permissions::with_read_view(std::move(slot), std::forward<Body>(body));
        if constexpr (std::is_same_v<decltype(lent), detail::hold_slot_t<Loan>>) {
            slot = std::move(lent);
        } else {
            auto [value, loan] = std::move(lent);
            slot = std::move(loan);
            return value;
        }
    }

    // A body that holds a linear value by value binds to this overload, so
    // the refusal gives its reason (HoldsNoLinearValue of foundation/Linear.h).
    template <class Tag, class Set, class Body>
        requires(!::foundation::linear::HoldsNoLinearValue<Body>) && (detail::holds_loan_of(^^Set, ^^Tag))
                 && ::foundation::permissions::ReadViewBody<Body, Tag, detail::element_brand_t<Set, Tag>>
    static void read_slot(HoldSlots<Set>& slots, Body&& body) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

    // Spends the slots of a set with no loan open, and gives every token
    // back, in a Record in the order of the set.
    template <class... Elems>
        requires(!perm_set_has_open_loan_v<::foundation::permissions::PermSet<Elems...>>)
    [[nodiscard]] static constexpr ::foundation::core::Record<detail::hold_slot_t<Elems>...>
    tokens_of(HoldSlots<::foundation::permissions::PermSet<Elems...>>&& slots) noexcept {
        return tokens_from_(std::move(slots), std::index_sequence_for<Elems...>{});
    }

    // Spends the slots of a closed session with no loan open, and gives its
    // Resource and every token, in one Record with the Resource first.
    template <class Resource, class... Elems>
        requires(!perm_set_has_open_loan_v<::foundation::permissions::PermSet<Elems...>>)
             && (!std::is_reference_v<Resource>)
    [[nodiscard]] static constexpr ::foundation::core::Record<Resource, detail::hold_slot_t<Elems>...>
    close_slots(Resource resource, HoldSlots<::foundation::permissions::PermSet<Elems...>>&& slots) noexcept(
        std::is_nothrow_move_constructible_v<Resource>) {
        return tokens_from_(std::move(slots), std::index_sequence_for<Elems...>{}, std::move(resource));
    }

    // ── The hold ───────────────────────────────────────────────────

    // Consumes the tokens and builds the hold of their set.  The gate is
    // the gate of mint_permission_hold.
    template <class... Tags, class... Brands>
        requires ::foundation::permissions::DistinctTags<Tags...>
    [[nodiscard]] static constexpr PermHold<
        ::foundation::permissions::PermSet<::foundation::permissions::Permission<Tags, Brands>...>>
    from_tokens(::foundation::permissions::Permission<Tags, Brands>... tokens) noexcept {
        return hold_(slots_of(std::move(tokens)...));
    }

    template <class Tag, class Set>
        requires(detail::PlainTag<Tag> && detail::holds_token_of(^^Set, ^^Tag))
    [[nodiscard]] static constexpr auto take(PermHold<Set>&& hold) noexcept -> ::foundation::core::Record<
        detail::element_of_t<Set, Tag>,
        PermHold<::foundation::permissions::perm_set_remove_t<Set, detail::element_of_t<Set, Tag>>>> {
        auto [token, rest] = take_slot<Tag>(spend_(std::move(hold)));
        return {std::move(token), hold_(std::move(rest))};
    }

    template <class Set, class Tag, class Brand>
        requires(detail::PlainTag<Tag> && detail::regions_disjoint(^^Set, ^^::foundation::permissions::PermSet<Tag>))
    [[nodiscard]] static constexpr auto put(PermHold<Set>&& hold,
                                            ::foundation::permissions::Permission<Tag, Brand> token) noexcept
        -> PermHold<
            ::foundation::permissions::perm_set_insert_t<Set, ::foundation::permissions::Permission<Tag, Brand>>> {
        return hold_(put_slot(spend_(std::move(hold)), std::move(token)));
    }

    // The marker takes the brand of the token that the hold keeps for the
    // tag.
    template <class Tag, class Set, class T>
        requires detail::HoldCanPack<Set, Tag, T>
    [[nodiscard]] static constexpr auto pack(PermHold<Set>&& hold, T value) noexcept -> ::foundation::core::Record<
        Transferable<T, Tag, detail::element_brand_t<Set, Tag>>,
        PermHold<::foundation::permissions::perm_set_remove_t<Set, detail::element_of_t<Set, Tag>>>> {
        auto [message, rest] = message_from_slots<Transferable<T, Tag, detail::element_brand_t<Set, Tag>>>(
            spend_(std::move(hold)), std::move(value));
        return {std::move(message), hold_(std::move(rest))};
    }

    template <class Set, class Message>
        requires detail::MovesAToken<Message> && ReceivablePayload<Message, Set>
    [[nodiscard]] static constexpr auto unpack(PermHold<Set>&& hold, Message&& message) noexcept
        -> ::foundation::core::Record<typename Message::payload_type, PermHold<perm_set_after_recv_t<Set, Message>>> {
        auto [value, rest] = value_into_slots(spend_(std::move(hold)), std::move(message));
        return {std::move(value), hold_(std::move(rest))};
    }

    // The loan takes the brand of the token that the hold keeps for the tag.
    template <class Tag, class Set, class T>
        requires detail::HoldCanLend<Set, Tag, T>
    [[nodiscard]] static constexpr auto lend(PermHold<Set>&& hold, T value) noexcept -> ::foundation::core::Record<
        Borrowed<T, Tag, detail::element_brand_t<Set, Tag>>,
        PermHold<perm_set_after_send_t<Set, Borrowed<T, Tag, detail::element_brand_t<Set, Tag>>>>> {
        auto [message, rest] = message_from_slots<Borrowed<T, Tag, detail::element_brand_t<Set, Tag>>>(
            spend_(std::move(hold)), std::move(value));
        return {std::move(message), hold_(std::move(rest))};
    }

    template <class Set, class T, class Tag, class Brand>
        requires ReceivablePayload<Released<T, Tag, Brand>, Set>
    [[nodiscard]] static constexpr auto end_loan(PermHold<Set>&& hold, Released<T, Tag, Brand>&& message) noexcept
        -> ::foundation::core::Record<T, PermHold<perm_set_after_recv_t<Set, Released<T, Tag, Brand>>>> {
        auto [value, rest] = value_into_slots(spend_(std::move(hold)), std::move(message));
        return {std::move(value), hold_(std::move(rest))};
    }

    template <class Set, class T, class Tag, class Brand>
        requires detail::HoldCanAcceptLoan<Set, T, Tag, Brand>
    [[nodiscard]] static constexpr auto accept_loan(PermHold<Set>&& hold, Borrowed<T, Tag, Brand>&& message) noexcept
        -> ::foundation::core::Record<T, PermHold<perm_set_after_recv_t<Set, Borrowed<T, Tag, Brand>>>> {
        auto [value, rest] = value_into_slots(spend_(std::move(hold)), std::move(message));
        return {std::move(value), hold_(std::move(rest))};
    }

    // The release takes the brand of the loan that the hold keeps for the
    // tag.
    template <class Tag, class Set, class T>
        requires detail::HoldCanRelease<Set, Tag, T>
    [[nodiscard]] static constexpr auto release(PermHold<Set>&& hold, T value) noexcept -> ::foundation::core::Record<
        Released<T, Tag, detail::element_brand_t<Set, Tag>>,
        PermHold<perm_set_after_send_t<Set, Released<T, Tag, detail::element_brand_t<Set, Tag>>>>> {
        auto [message, rest] = message_from_slots<Released<T, Tag, detail::element_brand_t<Set, Tag>>>(
            spend_(std::move(hold)), std::move(value));
        return {std::move(message), hold_(std::move(rest))};
    }

    // Spends the hold of a set with no loan open and gives every token back.
    template <class Set>
        requires(!perm_set_has_open_loan_v<Set>)
    [[nodiscard]] static constexpr auto tokens_of(PermHold<Set>&& hold) noexcept {
        return tokens_of(spend_(std::move(hold)));
    }
};

// Consumes the tokens and returns the hold of their set.  Each token is
// a parameter by value, so an lvalue token is refused: it would leave a
// second name for a token that the hold now owns.  One tag twice is
// refused, because a set holds each tag once.  Each token keeps its brand, so
// into_permissions gives back the tokens that the hold took.
template <class... Tags, class... Brands>
    requires ::foundation::permissions::DistinctTags<Tags...>
[[nodiscard]] constexpr PermHold<
    ::foundation::permissions::PermSet<::foundation::permissions::Permission<Tags, Brands>...>>
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
        requires(detail::PlainTag<Tag> && detail::holds_token_of(^^Set, ^^Tag))
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
        requires detail::HoldCanPack<Set, Tag, T>
    [[nodiscard]] constexpr auto pack(T value) && noexcept {
        return HoldFactory::pack<Tag>(std::move(*this), std::move(value));
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

    template <class T, class Tag, class Brand>
        requires ReceivablePayload<Released<T, Tag, Brand>, Set>
    [[nodiscard]] constexpr auto
    end_loan(Released<T, Tag, Brand>&& message) && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return HoldFactory::end_loan(std::move(*this), std::move(message));
    }

    // ── A read loan, borrower side ─────────────────────────────────

    template <class T, class Tag, class Brand>
        requires detail::HoldCanAcceptLoan<Set, T, Tag, Brand>
    [[nodiscard]] constexpr auto
    accept_loan(Borrowed<T, Tag, Brand>&& message) && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return HoldFactory::accept_loan(std::move(*this), std::move(message));
    }

    // Runs the body with a read view of a region that this hold borrows,
    // and hands the hold back.  The hold is consumed for the call, so a
    // body that names the hold through a capture finds it spent, and a
    // transition on it aborts.  A body that returns nothing gives the
    // hold back, and a body that returns a value gives a Record of that
    // value and the hold.
    template <class Tag, class Body>
        requires(detail::holds_loan_of(^^Set, ^^Tag))
             && ::foundation::permissions::ReadViewBody<Body, Tag, detail::element_brand_t<Set, Tag>>
             && ::foundation::linear::HoldsNoLinearValue<Body>
    [[nodiscard]] constexpr auto read(Body&& body) && noexcept(
        ::foundation::permissions::detail::read_view_door_nothrow_v<detail::hold_slot_t<detail::element_of_t<Set, Tag>>,
                                                                    Body>) {
        require_live_();
        PermHold held{std::move(*this)};
        using Result = decltype(HoldFactory::read_slot<Tag>(held.slots_, std::forward<Body>(body)));
        if constexpr (std::is_void_v<Result>) {
            HoldFactory::read_slot<Tag>(held.slots_, std::forward<Body>(body));
            return held;
        } else {
            Result value = HoldFactory::read_slot<Tag>(held.slots_, std::forward<Body>(body));
            return ::foundation::core::Record{std::move(value), std::move(held)};
        }
    }

    // A body that holds a linear value by value binds to this overload, so
    // the refusal gives its reason (HoldsNoLinearValue of foundation/Linear.h).
    template <class Tag, class Body>
        requires(!::foundation::linear::HoldsNoLinearValue<Body>) && (detail::holds_loan_of(^^Set, ^^Tag))
                 && ::foundation::permissions::ReadViewBody<Body, Tag, detail::element_brand_t<Set, Tag>>
    void read(Body&& body) && = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

    template <class Tag, class T>
        requires detail::HoldCanRelease<Set, Tag, T>
    [[nodiscard]] constexpr auto release(T value) && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return HoldFactory::release<Tag>(std::move(*this), std::move(value));
    }

    // ── The end of the hold ────────────────────────────────────────

    // Hands every token back, in a Record in the order of the set.  A hold
    // with a loan open has no tokens to hand back for the loan, so it
    // cannot end here.
    [[nodiscard]] constexpr ::foundation::core::Record<detail::hold_slot_t<Elems>...> into_permissions() && noexcept
        requires(!perm_set_has_open_loan_v<Set>)
    {
        return HoldFactory::tokens_of(std::move(*this));
    }

private:
    friend class HoldFactory;

    constexpr explicit PermHold(detail::hold_from_slots, HoldSlots<Set>&& slots) noexcept : slots_{std::move(slots)} {}

    constexpr void require_live_() const noexcept {
        if (!live_) [[unlikely]]
            detail::hold_consumed_abort_();
    }

    HoldSlots<Set> slots_;
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
    // A hand-off delegates too.  Its family stands in
    // fixy/session/Delegate.h, which this header does not include, so the
    // check file of that header asserts it.
    using accepts = witnesses<::fixy::session::detail::delegation_armed_witness::HoldsEndpoint,
                              ::fixy::session::detail::delegation_armed_witness::NamesItsProtocol*, void*>;
    using refuses =
        witnesses<int, ::fixy::session::detail::delegation_armed_witness::PlainMessage,
                  ::fixy::session::Send<int, ::fixy::session::End>,
                  ::fixy::session::VendorPinned<::fixy::session::VendorBackend::Portable, ::fixy::session::End>>;
};
