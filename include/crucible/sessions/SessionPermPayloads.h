#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::safety::proto::Transferable / Borrowed / Returned —
// session-payload markers signalling permission flow at the message
// level.
//
// Phase 2 of FOUND-C (#607 + #608 + #609).  See `misc/27_04_csl_
// permission_session_wiring.md` §7 for the full spec.
//
// ─── What this header is ───────────────────────────────────────────
//
// Four payload wrappers + their per-marker permission-flow dispatch
// metafunctions.  PermissionedSessionHandle (Phase 3) reads the
// payload's marker shape and evolves its PermSet accordingly:
//
//   Transferable<T, X>  sender LOSES Permission<X>;  recipient GAINS it.
//   Borrowed<T, X>      sender LENDS read access scoped to recipient's
//                       next protocol step (recipient gets ReadView<X>).
//   Returned<T, X>      sender RETURNS a previously-borrowed Permission
//                       to its origin; recipient GAINS it.
//   DelegatedSession<P, PS>
//                       sender LOSES every token in inner PermSet PS;
//                       recipient GAINS that inner PermSet with the
//                       delegated endpoint.
//   Plain T             no permission flow (the default).
//
// PermSet evolution table (the central dispatch — wiring plan §7.2):
//
//   Send/Recv shape                             PermSet evolution
//   --------------------------------------------------------------------
//   Send<Plain T, K>                            PS' = PS
//   Send<Transferable<T, X>, K>                 PS' = remove<PS, X>
//   Send<Borrowed<T, X>, K>                     PS' = PS  (borrow scoped)
//   Send<Returned<T, X>, K>                     PS' = remove<PS, X>
//   Send<DelegatedSession<P, InnerPS>, K>       PS' = PS \ InnerPS
//   Recv<Plain T, K>                            PS' = PS
//   Recv<Transferable<T, X>, K>                 PS' = insert<PS, X>
//   Recv<Borrowed<T, X>, K>                     PS' = PS  (ReadView only)
//   Recv<Returned<T, X>, K>                     PS' = insert<PS, X>
//   Recv<DelegatedSession<P, InnerPS>, K>       PS' = PS ∪ InnerPS
//
// ─── Why three markers, not just one ───────────────────────────────
//
// Transferable covers the common case: producer sends a permission to
// consumer, consumer gains exclusive access.  Two real-world patterns
// need richer payloads:
//
//   * Borrowed: Vessel dispatch lends the bg drainer a *read-only
//     view* of TraceEntry data; the borrow is scoped to the recipient's
//     next step.  Encoded as Borrowed<TraceEntry, TraceRingTag> — the
//     recipient gets ReadView<TraceRingTag> for the duration.
//     Composes with the existing permissions/ReadView.h discipline.
//
//   * Returned: Cipher tier promotion — hot-tier delegates the entry's
//     session to warm-tier with Permission<HotEntry>.  Once warm-tier
//     finishes, it Returned<DurabilityAck, HotEntry> the permission
//     back.  Without Returned, the round-trip would require two
//     separate Transferables and PS bookkeeping at the protocol level
//     rather than the message level.
//
// DelegatedSession extends the marker family for Honda 1998
// throw/catch: the payload is a session endpoint, and the endpoint's
// own PermSet moves with it.  This header ships the marker and pure
// PermSet evolution; PermissionedSessionHandle specialisations that
// use it arrive in the next layer.
//
// ─── Composition with SessionPayloadSubsort.h ──────────────────────
//
// The two layers compose orthogonally:
//
//   * Permission flow (this header) dispatches on payload SHAPE
//     (Transferable / Borrowed / Returned / plain).  Independent of
//     the carried T's grade.
//   * Subsumption rules (SessionPayloadSubsort.h) determine which
//     payload TYPES are interchangeable through Send's covariance /
//     Recv's contravariance.  Independent of permission flow.
//
// PermissionedSessionHandle weaves both: the wrapper applies
// permission-flow rules to evolve PS at every step; subsumption rules
// already apply to the inner Send/Recv combinators it wraps.  Both
// rule sets see disjoint information; neither sees the other's tags;
// the composition is correct by construction.  See
// SessionPayloadSubsort.h:90-99 for the integration anticipation
// comment from before this header shipped.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/permissions/PermSet.h>
#include <crucible/permissions/Permission.h>
#include <crucible/permissions/ReadView.h>

#include <type_traits>
#include <utility>

namespace crucible::safety::proto {

// ── Transferable<T, Tag> ─────────────────────────────────────────────
//
// Send<Transferable<T, X>, K>:   sender loses Permission<X>.
// Recv<Transferable<T, X>, K>:   recipient gains Permission<X>.
//
// The token field is [[no_unique_address]] so sizeof(Transferable<T, X>)
// == sizeof(T) when T is not also empty.  Move-only — copying would
// violate Permission's linearity.

template <typename T, typename Tag>
struct [[nodiscard]] Transferable {
    using payload_type     = T;
    using transferred_perm = Tag;

    T                                              value;
    [[no_unique_address]] ::crucible::safety::Permission<Tag> perm;

    constexpr Transferable(T v, ::crucible::safety::Permission<Tag>&& p) noexcept
        : value{std::move(v)}, perm{std::move(p)} {}

    Transferable(const Transferable&)            = delete;
    Transferable& operator=(const Transferable&) = delete;
    constexpr Transferable(Transferable&&) noexcept            = default;
    constexpr Transferable& operator=(Transferable&&) noexcept = default;
    ~Transferable() = default;
};

// ── Borrowed<T, Tag> ────────────────────────────────────────────────
//
// Send<Borrowed<T, X>, K>:   sender keeps Permission<X>; recipient gets
//                            a ReadView<X> scoped to the next protocol
//                            step (PS unchanged on both sides).
// Recv<Borrowed<T, X>, K>:   recipient receives the ReadView; PS unchanged.
//
// Copyable because ReadView is — multiple borrowers may coexist freely.

template <typename T, typename Tag>
struct [[nodiscard]] Borrowed {
    using payload_type  = T;
    using borrowed_perm = Tag;

    T                                            value;
    [[no_unique_address]] ::crucible::safety::ReadView<Tag> view;

    constexpr Borrowed(T v, ::crucible::safety::ReadView<Tag> rv = {}) noexcept
        : value{std::move(v)}, view{rv} {}

    constexpr Borrowed(const Borrowed&) noexcept            = default;
    constexpr Borrowed(Borrowed&&) noexcept                 = default;
    constexpr Borrowed& operator=(const Borrowed&) noexcept = default;
    constexpr Borrowed& operator=(Borrowed&&) noexcept      = default;
    ~Borrowed() = default;
};

// ── Returned<T, Tag> ────────────────────────────────────────────────
//
// Send<Returned<T, X>, K>:   sender returns a previously-borrowed
//                            Permission<X> to its origin;
//                            sender loses Permission<X>.
// Recv<Returned<T, X>, K>:   recipient gains Permission<X>.
//
// Symmetric to Transferable in PermSet evolution; semantically
// distinct because the type system tracks the round-trip pattern at
// the protocol level (see CipherTierPromotion in `SessionPatterns.h`
// for the canonical use case once Phase 6 of the integration ships).

template <typename T, typename Tag>
struct [[nodiscard]] Returned {
    using payload_type = T;
    using returned     = Tag;

    T                                              value;
    [[no_unique_address]] ::crucible::safety::Permission<Tag> returned_perm;

    constexpr Returned(T v, ::crucible::safety::Permission<Tag>&& p) noexcept
        : value{std::move(v)}, returned_perm{std::move(p)} {}

    Returned(const Returned&)            = delete;
    Returned& operator=(const Returned&) = delete;
    constexpr Returned(Returned&&) noexcept            = default;
    constexpr Returned& operator=(Returned&&) noexcept = default;
    ~Returned() = default;
};

// ── DelegatedSession<InnerProto, InnerPS> ──────────────────────────
//
// Payload marker for higher-order session handoff with permissions.
// Send<DelegatedSession<P, PS>, K> transfers an existing endpoint of
// protocol P together with every permission token in PS.  The marker
// is type-only: the runtime endpoint resource is moved by Delegate /
// Accept transport code, while this wrapper tells the type-level
// PermSet evolution how authority moves across the same handoff.

template <typename InnerProto, typename InnerPS>
struct [[nodiscard]] DelegatedSession {
    using inner_proto    = InnerProto;
    using inner_perm_set = InnerPS;
};

// ── Marker recognisers ──────────────────────────────────────────────

namespace detail {

template <typename T> struct is_transferable_impl : std::false_type {};
template <typename T, typename Tag>
struct is_transferable_impl<Transferable<T, Tag>> : std::true_type {
    using transferred_perm = Tag;
};

template <typename T> struct is_borrowed_impl : std::false_type {};
template <typename T, typename Tag>
struct is_borrowed_impl<Borrowed<T, Tag>> : std::true_type {
    using borrowed_perm = Tag;
};

template <typename T> struct is_returned_impl : std::false_type {};
template <typename T, typename Tag>
struct is_returned_impl<Returned<T, Tag>> : std::true_type {
    using returned = Tag;
};

template <typename T> struct is_delegated_session_impl : std::false_type {};
template <typename InnerProto, typename InnerPS>
struct is_delegated_session_impl<DelegatedSession<InnerProto, InnerPS>>
    : std::true_type {
    using inner_proto    = InnerProto;
    using inner_perm_set = InnerPS;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_transferable_v =
    detail::is_transferable_impl<std::remove_cvref_t<T>>::value;

template <typename T>
inline constexpr bool is_borrowed_v =
    detail::is_borrowed_impl<std::remove_cvref_t<T>>::value;

template <typename T>
inline constexpr bool is_returned_v =
    detail::is_returned_impl<std::remove_cvref_t<T>>::value;

template <typename T>
inline constexpr bool is_delegated_session_v =
    detail::is_delegated_session_impl<std::remove_cvref_t<T>>::value;

template <typename T>
inline constexpr bool is_plain_payload_v =
    !is_transferable_v<T> && !is_borrowed_v<T> &&
    !is_returned_v<T> && !is_delegated_session_v<T>;

// ── Tag extraction ──────────────────────────────────────────────────
//
// payload_perm_tag_t<T>: the Tag the payload affects, or `void` if T
// is a plain payload.  Used by PermissionedSessionHandle's per-step
// PermSet evolution.

namespace detail {

template <typename T, bool IsTransferable, bool IsBorrowed, bool IsReturned>
struct payload_perm_tag_branch {
    using type = void;  // plain payload
};

template <typename T, bool IsBorrowed, bool IsReturned>
struct payload_perm_tag_branch<T, /*Transferable=*/true, IsBorrowed, IsReturned> {
    using type = typename is_transferable_impl<
        std::remove_cvref_t<T>>::transferred_perm;
};

template <typename T, bool IsReturned>
struct payload_perm_tag_branch<T, /*Transferable=*/false, /*Borrowed=*/true, IsReturned> {
    using type = typename is_borrowed_impl<
        std::remove_cvref_t<T>>::borrowed_perm;
};

template <typename T>
struct payload_perm_tag_branch<T, /*Transferable=*/false, /*Borrowed=*/false, /*Returned=*/true> {
    using type = typename is_returned_impl<
        std::remove_cvref_t<T>>::returned;
};

}  // namespace detail

template <typename T>
struct payload_perm_tag {
    using type = typename detail::payload_perm_tag_branch<
        T,
        is_transferable_v<T>,
        is_borrowed_v<T>,
        is_returned_v<T>>::type;
};

template <typename T>
using payload_perm_tag_t = typename payload_perm_tag<T>::type;

template <typename T>
struct delegated_session_inner_proto {
    using type = typename detail::is_delegated_session_impl<
        std::remove_cvref_t<T>>::inner_proto;
};

template <typename T>
using delegated_session_inner_proto_t =
    typename delegated_session_inner_proto<T>::type;

template <typename T>
struct delegated_session_perm_set {
    using type = typename detail::is_delegated_session_impl<
        std::remove_cvref_t<T>>::inner_perm_set;
};

template <typename T>
using delegated_session_perm_set_t =
    typename delegated_session_perm_set<T>::type;

// ── compute_perm_set_after_send / _after_recv ───────────────────────
//
// The central dispatch table from §7.2 of the wiring plan, encoded as
// metafunctions over (PS, T).  Every case in the table returns the
// resulting PermSet type.

namespace detail {

template <typename PS, typename T,
          bool IsTransferable = is_transferable_v<T>,
          bool IsBorrowed     = is_borrowed_v<T>,
          bool IsReturned     = is_returned_v<T>>
struct send_evolve;

// Plain payload: PS unchanged.
template <typename PS, typename T>
struct send_evolve<PS, T, /*Transferable=*/false, /*Borrowed=*/false, /*Returned=*/false> {
    using type = PS;
};

// Transferable: sender LOSES the perm.
template <typename PS, typename T, bool IsBorrowed, bool IsReturned>
struct send_evolve<PS, T, /*Transferable=*/true, IsBorrowed, IsReturned> {
    using tag  = typename is_transferable_impl<std::remove_cvref_t<T>>::transferred_perm;
    using type = perm_set_remove_t<PS, tag>;
};

// Borrowed: PS unchanged (scoped lend).
template <typename PS, typename T, bool IsReturned>
struct send_evolve<PS, T, /*Transferable=*/false, /*Borrowed=*/true, IsReturned> {
    using type = PS;
};

// Returned: sender RETURNS the perm (loses it).
template <typename PS, typename T>
struct send_evolve<PS, T, /*Transferable=*/false, /*Borrowed=*/false, /*Returned=*/true> {
    using tag  = typename is_returned_impl<std::remove_cvref_t<T>>::returned;
    using type = perm_set_remove_t<PS, tag>;
};

template <typename PS, typename T,
          bool IsTransferable = is_transferable_v<T>,
          bool IsBorrowed     = is_borrowed_v<T>,
          bool IsReturned     = is_returned_v<T>>
struct recv_evolve;

// Plain: unchanged.
template <typename PS, typename T>
struct recv_evolve<PS, T, /*Transferable=*/false, /*Borrowed=*/false, /*Returned=*/false> {
    using type = PS;
};

// Transferable: recipient GAINS the perm.
template <typename PS, typename T, bool IsBorrowed, bool IsReturned>
struct recv_evolve<PS, T, /*Transferable=*/true, IsBorrowed, IsReturned> {
    using tag  = typename is_transferable_impl<std::remove_cvref_t<T>>::transferred_perm;
    using type = perm_set_insert_t<PS, tag>;
};

// Borrowed: PS unchanged (recipient gets ReadView only).
template <typename PS, typename T, bool IsReturned>
struct recv_evolve<PS, T, /*Transferable=*/false, /*Borrowed=*/true, IsReturned> {
    using type = PS;
};

// Returned: recipient GAINS the returning perm.
template <typename PS, typename T>
struct recv_evolve<PS, T, /*Transferable=*/false, /*Borrowed=*/false, /*Returned=*/true> {
    using tag  = typename is_returned_impl<std::remove_cvref_t<T>>::returned;
    using type = perm_set_insert_t<PS, tag>;
};

}  // namespace detail

template <typename PS, typename T>
struct compute_perm_set_after_send : detail::send_evolve<PS, T> {};

template <typename PS, typename InnerProto, typename InnerPS>
struct compute_perm_set_after_send<PS, DelegatedSession<InnerProto, InnerPS>> {
    static_assert(perm_set_subset_v<InnerPS, PS>,
        "crucible::session::diagnostic [PermissionImbalance]: "
        "Send<DelegatedSession<P, InnerPS>, K> requires the sender "
        "PermSet to contain every token in InnerPS before delegation. "
        "The inner endpoint's authority moves with the delegated "
        "session handle; mint or transfer those permissions before "
        "attempting the handoff.");
    using type = perm_set_difference_t<PS, InnerPS>;
};

template <typename PS, typename T>
using compute_perm_set_after_send_t =
    typename compute_perm_set_after_send<PS, T>::type;

template <typename PS, typename T>
struct compute_perm_set_after_recv : detail::recv_evolve<PS, T> {};

template <typename PS, typename InnerProto, typename InnerPS>
struct compute_perm_set_after_recv<PS, DelegatedSession<InnerProto, InnerPS>> {
    using type = perm_set_union_t<PS, InnerPS>;
};

template <typename PS, typename T>
using compute_perm_set_after_recv_t =
    typename compute_perm_set_after_recv<PS, T>::type;

// ── apply_payload_permission<Payload, SenderPS, RecipientPS> ───────
//
// Pair-form dispatch used by handoff code that wants both sides'
// PermSet evolution at once.  The per-side compute_* aliases above are
// still the single-source rules; this wrapper only packages the two
// results under stable names.

template <typename SenderPS, typename RecipientPS>
struct PayloadPermissionResult {
    using sender_perm_set    = SenderPS;
    using recipient_perm_set = RecipientPS;
};

template <typename Payload, typename SenderPS, typename RecipientPS>
struct apply_payload_permission {
    using type = PayloadPermissionResult<
        compute_perm_set_after_send_t<SenderPS, Payload>,
        compute_perm_set_after_recv_t<RecipientPS, Payload>>;
};

template <typename Payload, typename SenderPS, typename RecipientPS>
using apply_payload_permission_t =
    typename apply_payload_permission<Payload, SenderPS, RecipientPS>::type;

template <typename Payload, typename SenderPS, typename RecipientPS>
using apply_payload_permission_sender_t =
    typename apply_payload_permission_t<
        Payload, SenderPS, RecipientPS>::sender_perm_set;

template <typename Payload, typename SenderPS, typename RecipientPS>
using apply_payload_permission_recipient_t =
    typename apply_payload_permission_t<
        Payload, SenderPS, RecipientPS>::recipient_perm_set;

// ── SendablePayload concept ─────────────────────────────────────────
//
// Send precondition: the payload's permission demand is satisfied by
// the handle's current PermSet.
//
//   * Plain payloads:        always sendable (no permission demand).
//   * Borrowed<T, X>:        always sendable (the lend creates the view
//                            from the holder's permission, so the sender
//                            inherently holds X — but the type system
//                            cannot inspect ReadView's source, so we
//                            accept and rely on the borrow's runtime
//                            origin discipline).
//   * Transferable<T, X>:    sendable iff X ∈ PS.
//   * Returned<T, X>:        sendable iff X ∈ PS (same as Transferable).
//   * DelegatedSession<P, InnerPS>: sendable iff InnerPS ⊆ PS.
//
// The Recv-side has no analogous gate: receiving a payload always
// succeeds at the type-system level; the PermSet evolution captures
// the resulting permission acquisition.

template <typename T, typename PS>
concept SendablePayload =
    is_plain_payload_v<T>
    || is_borrowed_v<T>
    || (is_transferable_v<T>
        && perm_set_contains_v<PS, payload_perm_tag_t<T>>)
    || (is_returned_v<T>
        && perm_set_contains_v<PS, payload_perm_tag_t<T>>)
    || (is_delegated_session_v<T>
        && perm_set_subset_v<delegated_session_perm_set_t<T>, PS>);

template <typename T, typename PS>
concept ReceivablePayload = true;

}  // namespace crucible::safety::proto

// ═══════════════════════════════════════════════════════════════════
// Embedded smoke test — fires at every TU include.
// ═══════════════════════════════════════════════════════════════════

namespace crucible::safety::proto::detail::session_perm_payloads_smoke {

struct WorkPerm {};
struct HotPerm  {};
struct CfgPerm  {};
struct RequestResponseProto {};

using PS_empty = EmptyPermSet;
using PS_work  = PermSet<WorkPerm>;
using PS_hot   = PermSet<HotPerm>;
using PS_both  = PermSet<WorkPerm, HotPerm>;
using DelegatedWork = DelegatedSession<RequestResponseProto, PS_work>;

// ── Marker recognisers — positive ──────────────────────────────────
static_assert( is_transferable_v<Transferable<int, WorkPerm>>);
static_assert( is_transferable_v<const Transferable<int, WorkPerm>&>);
static_assert(!is_transferable_v<int>);
static_assert(!is_transferable_v<Borrowed<int, WorkPerm>>);
static_assert(!is_transferable_v<Returned<int, WorkPerm>>);

static_assert( is_borrowed_v<Borrowed<int, WorkPerm>>);
static_assert(!is_borrowed_v<int>);
static_assert(!is_borrowed_v<Transferable<int, WorkPerm>>);

static_assert( is_returned_v<Returned<int, WorkPerm>>);
static_assert(!is_returned_v<int>);
static_assert(!is_returned_v<Transferable<int, WorkPerm>>);

static_assert( is_delegated_session_v<DelegatedWork>);
static_assert(!is_delegated_session_v<int>);
static_assert(!is_delegated_session_v<Transferable<int, WorkPerm>>);

static_assert( is_plain_payload_v<int>);
static_assert( is_plain_payload_v<double>);
static_assert(!is_plain_payload_v<Transferable<int, WorkPerm>>);
static_assert(!is_plain_payload_v<Borrowed<int, WorkPerm>>);
static_assert(!is_plain_payload_v<Returned<int, WorkPerm>>);
static_assert(!is_plain_payload_v<DelegatedWork>);

// ── Tag extraction ─────────────────────────────────────────────────
static_assert(std::is_same_v<payload_perm_tag_t<int>, void>);
static_assert(std::is_same_v<
    payload_perm_tag_t<Transferable<int, WorkPerm>>, WorkPerm>);
static_assert(std::is_same_v<
    payload_perm_tag_t<Borrowed<int, HotPerm>>, HotPerm>);
static_assert(std::is_same_v<
    payload_perm_tag_t<Returned<int, CfgPerm>>, CfgPerm>);
static_assert(std::is_same_v<
    delegated_session_inner_proto_t<DelegatedWork>, RequestResponseProto>);
static_assert(perm_set_equal_v<
    delegated_session_perm_set_t<DelegatedWork>, PS_work>);

// ── compute_perm_set_after_send ────────────────────────────────────
//
// Plain: PS unchanged.
static_assert(std::is_same_v<
    compute_perm_set_after_send_t<PS_work, int>, PS_work>);
static_assert(std::is_same_v<
    compute_perm_set_after_send_t<PS_empty, int>, PS_empty>);

// Transferable: removes the tag.
static_assert(std::is_same_v<
    compute_perm_set_after_send_t<PS_work, Transferable<int, WorkPerm>>,
    PS_empty>);
static_assert(std::is_same_v<
    compute_perm_set_after_send_t<PS_both, Transferable<int, WorkPerm>>,
    PermSet<HotPerm>>);

// Borrowed: PS unchanged.
static_assert(std::is_same_v<
    compute_perm_set_after_send_t<PS_work, Borrowed<int, WorkPerm>>,
    PS_work>);

// Returned: removes the tag.
static_assert(std::is_same_v<
    compute_perm_set_after_send_t<PS_hot, Returned<int, HotPerm>>,
    PS_empty>);

// DelegatedSession: removes the entire inner PermSet from sender.
static_assert(perm_set_equal_v<
    compute_perm_set_after_send_t<PS_work, DelegatedWork>,
    PS_empty>);
static_assert(perm_set_equal_v<
    compute_perm_set_after_send_t<PS_both, DelegatedWork>,
    PS_hot>);

// ── compute_perm_set_after_recv ────────────────────────────────────
//
// Plain: PS unchanged.
static_assert(std::is_same_v<
    compute_perm_set_after_recv_t<PS_empty, int>, PS_empty>);

// Transferable: inserts the tag.
static_assert(std::is_same_v<
    compute_perm_set_after_recv_t<PS_empty, Transferable<int, WorkPerm>>,
    PermSet<WorkPerm>>);
static_assert(perm_set_equal_v<
    compute_perm_set_after_recv_t<PS_work, Transferable<int, HotPerm>>,
    PS_both>);

// Borrowed: PS unchanged (recipient gets ReadView only).
static_assert(std::is_same_v<
    compute_perm_set_after_recv_t<PS_empty, Borrowed<int, WorkPerm>>,
    PS_empty>);

// Returned: inserts the tag.
static_assert(std::is_same_v<
    compute_perm_set_after_recv_t<PS_empty, Returned<int, HotPerm>>,
    PermSet<HotPerm>>);

// DelegatedSession: inserts the whole inner PermSet into recipient.
static_assert(perm_set_equal_v<
    compute_perm_set_after_recv_t<PS_empty, DelegatedWork>,
    PS_work>);
static_assert(perm_set_equal_v<
    compute_perm_set_after_recv_t<PS_hot, DelegatedWork>,
    PS_both>);

// Pair-form dispatch mirrors per-side evolution.
using DelegatedApplied =
    apply_payload_permission_t<DelegatedWork, PS_both, PS_empty>;
static_assert(perm_set_equal_v<
    typename DelegatedApplied::sender_perm_set,
    PS_hot>);
static_assert(perm_set_equal_v<
    typename DelegatedApplied::recipient_perm_set,
    PS_work>);

// ── SendablePayload concept ────────────────────────────────────────
static_assert(SendablePayload<int, PS_empty>);             // plain always OK
static_assert(SendablePayload<int, PS_work>);
static_assert(SendablePayload<Borrowed<int, WorkPerm>, PS_empty>);  // borrow always OK at type level
static_assert(SendablePayload<Transferable<int, WorkPerm>, PS_work>);  // hold WorkPerm → can transfer
static_assert(!SendablePayload<Transferable<int, WorkPerm>, PS_empty>); // missing perm
static_assert(!SendablePayload<Transferable<int, HotPerm>, PS_work>);   // wrong tag
static_assert(SendablePayload<Returned<int, HotPerm>, PS_hot>);
static_assert(!SendablePayload<Returned<int, HotPerm>, PS_empty>);
static_assert(SendablePayload<DelegatedWork, PS_work>);
static_assert(SendablePayload<DelegatedWork, PS_both>);
static_assert(!SendablePayload<DelegatedWork, PS_empty>);

static_assert(ReceivablePayload<int, PS_empty>);
static_assert(ReceivablePayload<Transferable<int, HotPerm>, PS_empty>);

// ── sizeof discipline (EBO must fire) ──────────────────────────────
//
// Transferable<T, X> stores T plus a [[no_unique_address]] empty
// Permission<X>; under GCC 16's empty-base-optimisation the empty
// member shares offset with T's storage, so sizeof equals sizeof(T)
// exactly.  Asserting `==` (not `<=`) catches a future regression
// where a non-empty member sneaks in or [[no_unique_address]] gets
// dropped.  Verified on GCC 16.0.1 rawhide for int/char/double T.
static_assert(sizeof(Transferable<int,    WorkPerm>) == sizeof(int));
static_assert(sizeof(Transferable<char,   WorkPerm>) == sizeof(char));
static_assert(sizeof(Transferable<double, WorkPerm>) == sizeof(double));
static_assert(sizeof(Borrowed<int,    WorkPerm>) == sizeof(int));
static_assert(sizeof(Borrowed<char,   WorkPerm>) == sizeof(char));
static_assert(sizeof(Borrowed<double, WorkPerm>) == sizeof(double));
static_assert(sizeof(Returned<int,    WorkPerm>) == sizeof(int));
static_assert(sizeof(Returned<char,   WorkPerm>) == sizeof(char));
static_assert(sizeof(Returned<double, WorkPerm>) == sizeof(double));
static_assert(sizeof(DelegatedWork) == 1);

// Move-only discipline for permission-carrying markers.
static_assert(!std::is_copy_constructible_v<Transferable<int, WorkPerm>>);
static_assert( std::is_move_constructible_v<Transferable<int, WorkPerm>>);
static_assert(!std::is_copy_constructible_v<Returned<int, WorkPerm>>);
static_assert( std::is_move_constructible_v<Returned<int, WorkPerm>>);

// Borrowed is freely copyable (multiple borrowers OK).
static_assert(std::is_copy_constructible_v<Borrowed<int, WorkPerm>>);
static_assert(std::is_move_constructible_v<Borrowed<int, WorkPerm>>);

// ── runtime_smoke_test (per the discipline) ────────────────────────
inline void runtime_smoke_test() noexcept {
    // Round-trip a Transferable to make sure construction + move-only
    // chain compiles and links at runtime.
    auto perm = ::crucible::safety::mint_permission_root<WorkPerm>();
    Transferable<int, WorkPerm> t{42, std::move(perm)};
    Transferable<int, WorkPerm> t2 = std::move(t);
    (void)t2.value;

    // Borrowed: default-construct ReadView via aggregate init.
    Borrowed<int, CfgPerm> b{7};
    auto b_copy = b;  // copyable
    (void)b_copy.value;

    // Returned: same move-only shape as Transferable.
    auto perm2 = ::crucible::safety::mint_permission_root<HotPerm>();
    Returned<double, HotPerm> r{3.14, std::move(perm2)};
    auto r2 = std::move(r);
    (void)r2.value;

    // Concept checks at non-template-arg sites.
    static_assert(SendablePayload<int, PS_empty>);
    static_assert(SendablePayload<Transferable<int, WorkPerm>, PS_work>);
}

}  // namespace crucible::safety::proto::detail::session_perm_payloads_smoke
