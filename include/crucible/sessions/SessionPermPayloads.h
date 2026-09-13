#pragma once

// Payload markers that say what a message does to the sender's and the
// recipient's permission sets.  A protocol handle reads the marker on
// each payload and evolves its own set accordingly, so ownership moves
// at the message rather than through bookkeeping around the protocol.
//
//   Transferable      the sender gives up the token and the recipient
//                     takes it.
//   Borrowed          the sender keeps the token and the recipient gets
//                     a read view of it, so neither set changes.
//   Returned          a token that was lent out goes home.  The sender
//                     gives it up and the recipient takes it, the same
//                     way a transfer moves, but the type records that
//                     this closes a round trip.  Without it the return
//                     leg would be a second transfer and the pairing
//                     would live in whatever code tracks the two.
//   DelegatedSession  the payload is an endpoint of another protocol,
//                     and every token that endpoint holds moves with it.
//   anything else     no permission moves.
//
// The markers dispatch on the shape of a payload and say nothing about
// the type carried inside it.  Payload subsumption is the reverse: it
// inspects the carried type and ignores the shape.  The two rule sets
// read disjoint information, which is why both can apply to the same
// payload without either needing to know about the other.

#include <crucible/Platform.h>
#include <crucible/permissions/PermSet.h>
#include <crucible/permissions/Permission.h>
#include <crucible/permissions/ReadView.h>

#include <type_traits>
#include <utility>

namespace crucible::safety::proto {

// Move-only, because a copy would leave two holders of a token that
// names exclusive access.

template <typename T, typename Tag>
struct [[nodiscard]] Transferable {
    using payload_type = T;
    using transferred_perm = Tag;

    T value;
    [[no_unique_address]] ::crucible::safety::Permission<Tag> perm;

    constexpr Transferable(T v, ::crucible::safety::Permission<Tag>&& p) noexcept
        : value{std::move(v)}, perm{std::move(p)} {}

    Transferable(const Transferable&) = delete;
    Transferable& operator=(const Transferable&) = delete;
    constexpr Transferable(Transferable&&) noexcept = default;
    constexpr Transferable& operator=(Transferable&&) noexcept = default;
    ~Transferable() = default;
};

// Copyable, since a read view neither excludes another reader nor takes
// anything away from the holder.

template <typename T, typename Tag>
struct [[nodiscard]] Borrowed {
    using payload_type = T;
    using borrowed_perm = Tag;

    T value;
    [[no_unique_address]] ::crucible::safety::ReadView<Tag> view;

    constexpr Borrowed(T v, ::crucible::safety::ReadView<Tag> rv = {}) noexcept : value{std::move(v)}, view{rv} {}

    constexpr Borrowed(const Borrowed&) noexcept = default;
    constexpr Borrowed(Borrowed&&) noexcept = default;
    constexpr Borrowed& operator=(const Borrowed&) noexcept = default;
    constexpr Borrowed& operator=(Borrowed&&) noexcept = default;
    ~Borrowed() = default;
};

template <typename T, typename Tag>
struct [[nodiscard]] Returned {
    using payload_type = T;
    using returned = Tag;

    T value;
    [[no_unique_address]] ::crucible::safety::Permission<Tag> returned_perm;

    constexpr Returned(T v, ::crucible::safety::Permission<Tag>&& p) noexcept
        : value{std::move(v)}, returned_perm{std::move(p)} {}

    Returned(const Returned&) = delete;
    Returned& operator=(const Returned&) = delete;
    constexpr Returned(Returned&&) noexcept = default;
    constexpr Returned& operator=(Returned&&) noexcept = default;
    ~Returned() = default;
};

// The marker carries no storage.  The endpoint itself is moved by the
// transport that performs the handoff, and this type exists only to
// tell the permission sets on both sides how authority moves with it.

template <typename InnerProto, typename InnerPS>
struct [[nodiscard]] DelegatedSession {
    using inner_proto = InnerProto;
    using inner_perm_set = InnerPS;
};

namespace detail {

template <typename T>
struct is_transferable_impl : std::false_type {};
template <typename T, typename Tag>
struct is_transferable_impl<Transferable<T, Tag>> : std::true_type {
    using transferred_perm = Tag;
};

template <typename T>
struct is_borrowed_impl : std::false_type {};
template <typename T, typename Tag>
struct is_borrowed_impl<Borrowed<T, Tag>> : std::true_type {
    using borrowed_perm = Tag;
};

template <typename T>
struct is_returned_impl : std::false_type {};
template <typename T, typename Tag>
struct is_returned_impl<Returned<T, Tag>> : std::true_type {
    using returned = Tag;
};

template <typename T>
struct is_delegated_session_impl : std::false_type {};
template <typename InnerProto, typename InnerPS>
struct is_delegated_session_impl<DelegatedSession<InnerProto, InnerPS>> : std::true_type {
    using inner_proto = InnerProto;
    using inner_perm_set = InnerPS;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_transferable_v = detail::is_transferable_impl<std::remove_cvref_t<T>>::value;

template <typename T>
inline constexpr bool is_borrowed_v = detail::is_borrowed_impl<std::remove_cvref_t<T>>::value;

template <typename T>
inline constexpr bool is_returned_v = detail::is_returned_impl<std::remove_cvref_t<T>>::value;

template <typename T>
inline constexpr bool is_delegated_session_v = detail::is_delegated_session_impl<std::remove_cvref_t<T>>::value;

template <typename T>
inline constexpr bool is_plain_payload_v =
    !is_transferable_v<T> && !is_borrowed_v<T> && !is_returned_v<T> && !is_delegated_session_v<T>;

// A payload that moves no permission yields void for its tag.

namespace detail {

template <typename T, bool IsTransferable, bool IsBorrowed, bool IsReturned>
struct payload_perm_tag_branch {
    using type = void;
};

template <typename T, bool IsBorrowed, bool IsReturned>
struct payload_perm_tag_branch<T, /*Transferable=*/true, IsBorrowed, IsReturned> {
    using type = typename is_transferable_impl<std::remove_cvref_t<T>>::transferred_perm;
};

template <typename T, bool IsReturned>
struct payload_perm_tag_branch<T, /*Transferable=*/false, /*Borrowed=*/true, IsReturned> {
    using type = typename is_borrowed_impl<std::remove_cvref_t<T>>::borrowed_perm;
};

template <typename T>
struct payload_perm_tag_branch<T, /*Transferable=*/false, /*Borrowed=*/false, /*Returned=*/true> {
    using type = typename is_returned_impl<std::remove_cvref_t<T>>::returned;
};

}  // namespace detail

template <typename T>
struct payload_perm_tag {
    using type =
        typename detail::payload_perm_tag_branch<T, is_transferable_v<T>, is_borrowed_v<T>, is_returned_v<T>>::type;
};

template <typename T>
using payload_perm_tag_t = typename payload_perm_tag<T>::type;

template <typename T>
struct delegated_session_inner_proto {
    using type = typename detail::is_delegated_session_impl<std::remove_cvref_t<T>>::inner_proto;
};

template <typename T>
using delegated_session_inner_proto_t = typename delegated_session_inner_proto<T>::type;

template <typename T>
struct delegated_session_perm_set {
    using type = typename detail::is_delegated_session_impl<std::remove_cvref_t<T>>::inner_perm_set;
};

template <typename T>
using delegated_session_perm_set_t = typename delegated_session_perm_set<T>::type;

namespace detail {

template <typename PS, typename T, bool IsTransferable = is_transferable_v<T>, bool IsBorrowed = is_borrowed_v<T>,
          bool IsReturned = is_returned_v<T>>
struct send_evolve;

template <typename PS, typename T>
struct send_evolve<PS, T, /*Transferable=*/false, /*Borrowed=*/false, /*Returned=*/false> {
    using type = PS;
};

template <typename PS, typename T, bool IsBorrowed, bool IsReturned>
struct send_evolve<PS, T, /*Transferable=*/true, IsBorrowed, IsReturned> {
    using tag = typename is_transferable_impl<std::remove_cvref_t<T>>::transferred_perm;
    using type = perm_set_remove_t<PS, tag>;
};

template <typename PS, typename T, bool IsReturned>
struct send_evolve<PS, T, /*Transferable=*/false, /*Borrowed=*/true, IsReturned> {
    using type = PS;
};

template <typename PS, typename T>
struct send_evolve<PS, T, /*Transferable=*/false, /*Borrowed=*/false, /*Returned=*/true> {
    using tag = typename is_returned_impl<std::remove_cvref_t<T>>::returned;
    using type = perm_set_remove_t<PS, tag>;
};

template <typename PS, typename T, bool IsTransferable = is_transferable_v<T>, bool IsBorrowed = is_borrowed_v<T>,
          bool IsReturned = is_returned_v<T>>
struct recv_evolve;

template <typename PS, typename T>
struct recv_evolve<PS, T, /*Transferable=*/false, /*Borrowed=*/false, /*Returned=*/false> {
    using type = PS;
};

template <typename PS, typename T, bool IsBorrowed, bool IsReturned>
struct recv_evolve<PS, T, /*Transferable=*/true, IsBorrowed, IsReturned> {
    using tag = typename is_transferable_impl<std::remove_cvref_t<T>>::transferred_perm;
    using type = perm_set_insert_t<PS, tag>;
};

template <typename PS, typename T, bool IsReturned>
struct recv_evolve<PS, T, /*Transferable=*/false, /*Borrowed=*/true, IsReturned> {
    using type = PS;
};

template <typename PS, typename T>
struct recv_evolve<PS, T, /*Transferable=*/false, /*Borrowed=*/false, /*Returned=*/true> {
    using tag = typename is_returned_impl<std::remove_cvref_t<T>>::returned;
    using type = perm_set_insert_t<PS, tag>;
};

}  // namespace detail

template <typename PS, typename T>
struct compute_perm_set_after_send : detail::send_evolve<PS, T> {};

template <typename PS, typename InnerProto, typename InnerPS>
struct compute_perm_set_after_send<PS, DelegatedSession<InnerProto, InnerPS>> {
    static_assert(perm_set_subset_v<InnerPS, PS>, "crucible::session::diagnostic [PermissionImbalance]: "
                                                  "Send<DelegatedSession<P, InnerPS>, K> requires the sender "
                                                  "PermSet to contain every token in InnerPS before delegation. "
                                                  "The inner endpoint's authority moves with the delegated "
                                                  "session handle; mint or transfer those permissions before "
                                                  "attempting the handoff.");
    using type = perm_set_difference_t<PS, InnerPS>;
};

template <typename PS, typename T>
using compute_perm_set_after_send_t = typename compute_perm_set_after_send<PS, T>::type;

template <typename PS, typename T>
struct compute_perm_set_after_recv : detail::recv_evolve<PS, T> {};

template <typename PS, typename InnerProto, typename InnerPS>
struct compute_perm_set_after_recv<PS, DelegatedSession<InnerProto, InnerPS>> {
    using type = perm_set_union_t<PS, InnerPS>;
};

template <typename PS, typename T>
using compute_perm_set_after_recv_t = typename compute_perm_set_after_recv<PS, T>::type;

// A convenience for handoff code that wants both sides at once.  The
// per-side rules above stay the single source, and this only packages
// their two results.

template <typename SenderPS, typename RecipientPS>
struct PayloadPermissionResult {
    using sender_perm_set = SenderPS;
    using recipient_perm_set = RecipientPS;
};

template <typename Payload, typename SenderPS, typename RecipientPS>
struct apply_payload_permission {
    using type = PayloadPermissionResult<compute_perm_set_after_send_t<SenderPS, Payload>,
                                         compute_perm_set_after_recv_t<RecipientPS, Payload>>;
};

template <typename Payload, typename SenderPS, typename RecipientPS>
using apply_payload_permission_t = typename apply_payload_permission<Payload, SenderPS, RecipientPS>::type;

template <typename Payload, typename SenderPS, typename RecipientPS>
using apply_payload_permission_sender_t =
    typename apply_payload_permission_t<Payload, SenderPS, RecipientPS>::sender_perm_set;

template <typename Payload, typename SenderPS, typename RecipientPS>
using apply_payload_permission_recipient_t =
    typename apply_payload_permission_t<Payload, SenderPS, RecipientPS>::recipient_perm_set;

// A payload may be sent only when the sender already holds what the
// payload gives away.
//
// A borrow is the exception and is accepted unconditionally.  A read
// view can only have come from a holder, so the sender does hold the
// token, but a view does not record where it came from and the type
// system has nothing to check against.  The guarantee here rests on how
// views are created, not on this concept.
//
// There is no matching gate on the receiving side.  Taking a payload
// always type-checks, and what the recipient gains is recorded by the
// set evolution rather than demanded up front.

template <typename T, typename PS>
concept SendablePayload = is_plain_payload_v<T> || is_borrowed_v<T>
                       || (is_transferable_v<T> && perm_set_contains_v<PS, payload_perm_tag_t<T>>)
                       || (is_returned_v<T> && perm_set_contains_v<PS, payload_perm_tag_t<T>>)
                       || (is_delegated_session_v<T> && perm_set_subset_v<delegated_session_perm_set_t<T>, PS>);

template <typename T, typename PS>
concept ReceivablePayload = true;

}  // namespace crucible::safety::proto

namespace crucible::safety::proto::detail::session_perm_payloads_smoke {

struct WorkPerm {};
struct HotPerm {};
struct CfgPerm {};
struct RequestResponseProto {};

using PS_empty = EmptyPermSet;
using PS_work = PermSet<WorkPerm>;
using PS_hot = PermSet<HotPerm>;
using PS_both = PermSet<WorkPerm, HotPerm>;
using DelegatedWork = DelegatedSession<RequestResponseProto, PS_work>;

static_assert(is_transferable_v<Transferable<int, WorkPerm>>);
static_assert(is_transferable_v<const Transferable<int, WorkPerm>&>);
static_assert(!is_transferable_v<int>);
static_assert(!is_transferable_v<Borrowed<int, WorkPerm>>);
static_assert(!is_transferable_v<Returned<int, WorkPerm>>);

static_assert(is_borrowed_v<Borrowed<int, WorkPerm>>);
static_assert(!is_borrowed_v<int>);
static_assert(!is_borrowed_v<Transferable<int, WorkPerm>>);

static_assert(is_returned_v<Returned<int, WorkPerm>>);
static_assert(!is_returned_v<int>);
static_assert(!is_returned_v<Transferable<int, WorkPerm>>);

static_assert(is_delegated_session_v<DelegatedWork>);
static_assert(!is_delegated_session_v<int>);
static_assert(!is_delegated_session_v<Transferable<int, WorkPerm>>);

static_assert(is_plain_payload_v<int>);
static_assert(is_plain_payload_v<double>);
static_assert(!is_plain_payload_v<Transferable<int, WorkPerm>>);
static_assert(!is_plain_payload_v<Borrowed<int, WorkPerm>>);
static_assert(!is_plain_payload_v<Returned<int, WorkPerm>>);
static_assert(!is_plain_payload_v<DelegatedWork>);

static_assert(std::is_same_v<payload_perm_tag_t<int>, void>);
static_assert(std::is_same_v<payload_perm_tag_t<Transferable<int, WorkPerm>>, WorkPerm>);
static_assert(std::is_same_v<payload_perm_tag_t<Borrowed<int, HotPerm>>, HotPerm>);
static_assert(std::is_same_v<payload_perm_tag_t<Returned<int, CfgPerm>>, CfgPerm>);
static_assert(std::is_same_v<delegated_session_inner_proto_t<DelegatedWork>, RequestResponseProto>);
static_assert(perm_set_equal_v<delegated_session_perm_set_t<DelegatedWork>, PS_work>);

static_assert(std::is_same_v<compute_perm_set_after_send_t<PS_work, int>, PS_work>);
static_assert(std::is_same_v<compute_perm_set_after_send_t<PS_empty, int>, PS_empty>);

static_assert(std::is_same_v<compute_perm_set_after_send_t<PS_work, Transferable<int, WorkPerm>>, PS_empty>);
static_assert(std::is_same_v<compute_perm_set_after_send_t<PS_both, Transferable<int, WorkPerm>>, PermSet<HotPerm>>);

static_assert(std::is_same_v<compute_perm_set_after_send_t<PS_work, Borrowed<int, WorkPerm>>, PS_work>);

static_assert(std::is_same_v<compute_perm_set_after_send_t<PS_hot, Returned<int, HotPerm>>, PS_empty>);

static_assert(perm_set_equal_v<compute_perm_set_after_send_t<PS_work, DelegatedWork>, PS_empty>);
static_assert(perm_set_equal_v<compute_perm_set_after_send_t<PS_both, DelegatedWork>, PS_hot>);

static_assert(std::is_same_v<compute_perm_set_after_recv_t<PS_empty, int>, PS_empty>);

static_assert(std::is_same_v<compute_perm_set_after_recv_t<PS_empty, Transferable<int, WorkPerm>>, PermSet<WorkPerm>>);
static_assert(perm_set_equal_v<compute_perm_set_after_recv_t<PS_work, Transferable<int, HotPerm>>, PS_both>);

static_assert(std::is_same_v<compute_perm_set_after_recv_t<PS_empty, Borrowed<int, WorkPerm>>, PS_empty>);

static_assert(std::is_same_v<compute_perm_set_after_recv_t<PS_empty, Returned<int, HotPerm>>, PermSet<HotPerm>>);

static_assert(perm_set_equal_v<compute_perm_set_after_recv_t<PS_empty, DelegatedWork>, PS_work>);
static_assert(perm_set_equal_v<compute_perm_set_after_recv_t<PS_hot, DelegatedWork>, PS_both>);

using DelegatedApplied = apply_payload_permission_t<DelegatedWork, PS_both, PS_empty>;
static_assert(perm_set_equal_v<typename DelegatedApplied::sender_perm_set, PS_hot>);
static_assert(perm_set_equal_v<typename DelegatedApplied::recipient_perm_set, PS_work>);

static_assert(SendablePayload<int, PS_empty>);
static_assert(SendablePayload<int, PS_work>);
static_assert(SendablePayload<Borrowed<int, WorkPerm>, PS_empty>);
static_assert(SendablePayload<Transferable<int, WorkPerm>, PS_work>);
static_assert(!SendablePayload<Transferable<int, WorkPerm>, PS_empty>);
static_assert(!SendablePayload<Transferable<int, HotPerm>, PS_work>);
static_assert(SendablePayload<Returned<int, HotPerm>, PS_hot>);
static_assert(!SendablePayload<Returned<int, HotPerm>, PS_empty>);
static_assert(SendablePayload<DelegatedWork, PS_work>);
static_assert(SendablePayload<DelegatedWork, PS_both>);
static_assert(!SendablePayload<DelegatedWork, PS_empty>);

static_assert(ReceivablePayload<int, PS_empty>);
static_assert(ReceivablePayload<Transferable<int, HotPerm>, PS_empty>);

// A marker is expected to cost exactly what its payload costs, since
// the token beside it is empty and shares the payload's storage.  The
// comparison is equality rather than an upper bound so that a member
// gaining size, or the address-sharing attribute going missing, fails
// here instead of quietly inflating every message.
static_assert(sizeof(Transferable<int, WorkPerm>) == sizeof(int));
static_assert(sizeof(Transferable<char, WorkPerm>) == sizeof(char));
static_assert(sizeof(Transferable<double, WorkPerm>) == sizeof(double));
static_assert(sizeof(Borrowed<int, WorkPerm>) == sizeof(int));
static_assert(sizeof(Borrowed<char, WorkPerm>) == sizeof(char));
static_assert(sizeof(Borrowed<double, WorkPerm>) == sizeof(double));
static_assert(sizeof(Returned<int, WorkPerm>) == sizeof(int));
static_assert(sizeof(Returned<char, WorkPerm>) == sizeof(char));
static_assert(sizeof(Returned<double, WorkPerm>) == sizeof(double));
static_assert(sizeof(DelegatedWork) == 1);

static_assert(!std::is_copy_constructible_v<Transferable<int, WorkPerm>>);
static_assert(std::is_move_constructible_v<Transferable<int, WorkPerm>>);
static_assert(!std::is_copy_constructible_v<Returned<int, WorkPerm>>);
static_assert(std::is_move_constructible_v<Returned<int, WorkPerm>>);

static_assert(std::is_copy_constructible_v<Borrowed<int, WorkPerm>>);
static_assert(std::is_move_constructible_v<Borrowed<int, WorkPerm>>);

inline void runtime_smoke_test() noexcept {
    auto perm = ::crucible::safety::mint_permission_root<WorkPerm>();
    Transferable<int, WorkPerm> t{42, std::move(perm)};
    Transferable<int, WorkPerm> t2 = std::move(t);
    (void)t2.value;

    Borrowed<int, CfgPerm> b{7};
    auto b_copy = b;
    (void)b_copy.value;

    auto perm2 = ::crucible::safety::mint_permission_root<HotPerm>();
    Returned<double, HotPerm> r{3.14, std::move(perm2)};
    auto r2 = std::move(r);
    (void)r2.value;

    static_assert(SendablePayload<int, PS_empty>);
    static_assert(SendablePayload<Transferable<int, WorkPerm>, PS_work>);
}

}  // namespace crucible::safety::proto::detail::session_perm_payloads_smoke
