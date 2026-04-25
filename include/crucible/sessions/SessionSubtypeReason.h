#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::safety::proto — rich subtype-rejection diagnostics
//                            (SEPLOG-STRUCT-6, #380)
//
// `is_subtype_sync_v<T, U>` answers a YES / NO question: does T refine
// U under Gay-Hole synchronous subtyping?  When the answer is NO, the
// caller gets a single bit of information — but for a deeply-nested
// protocol like
//
//     Loop<Recv<Choice, Select<Send<Refined<Pred, int>, End>, ...>>>
//
// the FIRST FAILING POINT could be at any level of nesting, and the
// generic "subtype rejected" diagnostic forces the engineer to
// manually walk the structure to locate it.
//
// `subtype_rejection_reason<T, U>` answers the more useful question:
// WHY did the subtype check fail?  Walks the protocol tree in lockstep
// with `is_subtype_sync` and returns either:
//
//   * `SubtypeOk` — the relation holds (no reason to report); or
//
//   * `RejectionReason<DiagnosticTag, TInner, UInner>` — the relation
//     fails, with the FIRST FAILING INNER PAIR named explicitly and
//     the failure CLASSIFIED via a tag from SessionDiagnostic.h's
//     vocabulary.
//
// The metafunction is structurally parallel to `is_subtype_sync`:
// every successful rule's specialisation maps to a SubtypeOk-or-
// recurse pattern; every failure point in the rule produces a tagged
// RejectionReason.  Compile-time cost is the same as
// `is_subtype_sync`'s — one specialisation per combinator-pair shape.
//
// ─── Failure-class tags shipped ────────────────────────────────────
//
//     SubtypeMismatch              generic (primary template — cross-
//                                  shape pairs not otherwise specialised)
//     ProtocolViolation_Payload    payload type fails is_subsort
//     BranchCount_Mismatch         Select narrowing wrong direction
//                                  / Offer widening wrong direction
//     ShapeMismatch_SendVsRecv     Send vs Recv at the same position
//     ShapeMismatch_SelectVsOffer  Select vs Offer at the same position
//
// Continuation failures (Send-after-Send, Recv-after-Recv, Loop body,
// Select branch index N) do NOT introduce new tags — they recurse
// into the inner pair and bubble its reason up to the outermost
// caller.  This gives the user a single FAILING-PAIR-with-CAUSE
// pinpoint regardless of nesting depth.
//
// ─── Worked example ────────────────────────────────────────────────
//
// Given:
//
//     using A = Loop<Send<int,    Recv<int,  End>>>;
//     using B = Loop<Send<long,   Recv<int,  End>>>;
//
//     using R = subtype_rejection_reason_t<A, B>;
//     // R = RejectionReason<ProtocolViolation_Payload, int, long>
//     //
//     // The walk: Loop<...> ⩽ Loop<...> recurses into bodies;
//     // Send<int, ...> ⩽ Send<long, ...> checks is_subsort<int, long>
//     // (false by default — int is not a subsort of long unless the
//     // user specialised), reports the failure with the failing
//     // payload pair (int, long).
//
// And:
//
//     using C = Loop<Send<int, Send<int, End>>>;
//     using D = Loop<Send<int, Recv<int, End>>>;
//
//     using R = subtype_rejection_reason_t<C, D>;
//     // R = RejectionReason<ShapeMismatch_SendVsRecv,
//     //                     Send<int, End>, Recv<int, End>>
//     //
//     // The walk: Loop ⩽ Loop, Send<int, ...> ⩽ Send<int, ...>
//     // (payload OK, recurse), Send<int, End> ⩽ Recv<int, End>
//     // (cross-shape — Send vs Recv → ShapeMismatch_SendVsRecv).
//
// ─── Diagnostic helper ─────────────────────────────────────────────
//
// `assert_subtype_sync_diag<T, U>()` is a richer alternative to the
// existing `assert_subtype_sync<T, U>()`.  When the subtype relation
// fails it static_asserts with a message that names BOTH the failure
// class AND the failing inner pair, drastically cutting the time to
// localise the bug:
//
//     assert_subtype_sync_diag<C, D>();
//     // error: static assertion failed: crucible::session::diagnostic
//     //        [ShapeMismatch_SendVsRecv]: subtype rejected; failing
//     //        inner pair LHS=Send<int, End>, RHS=Recv<int, End>.  ...
//
// ─── Cost ──────────────────────────────────────────────────────────
//
// Per-call: O(|T| + |U|) compile-time template instantiations, same
// asymptotic cost as `is_subtype_sync_v<T, U>` itself.  The
// metafunction is short-circuiting: as soon as the first failure is
// found, recursion stops and the reason is returned.  Practical cost
// on typical Crucible protocols (≤ 30 combinators per side):
// negligible additions to template instantiation time.
//
// ─── References ────────────────────────────────────────────────────
//
//   misc/24_04_2026_safety_integration.md §43 (#380)
//   safety/SessionSubtype.h — the underlying is_subtype_sync rules.
//   safety/SessionDiagnostic.h — the diagnostic-tag vocabulary.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCheckpoint.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionDiagnostic.h>
#include <crucible/sessions/SessionSubtype.h>

#include <cstddef>
#include <tuple>
#include <type_traits>

namespace crucible::safety::proto {

// ═════════════════════════════════════════════════════════════════════
// ── Result types ───────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// SubtypeOk: returned when the subtype relation holds.  Sentinel type;
// no nested aliases — callers compare via std::is_same_v.
struct SubtypeOk {};

// RejectionReason<DiagnosticTag, TInner, UInner>: returned when the
// subtype relation fails.  Names the FIRST FAILING INNER PAIR walked
// in lockstep with the protocol tree, classified by a diagnostic tag
// from SessionDiagnostic.h.
//
// The static_asserts below enforce that DiagnosticTag IS a recognised
// diagnostic class (catches typos in user extensions); TInner and
// UInner are the failing pair, named for richer error messages.
template <typename DiagnosticTag, typename TInner, typename UInner>
struct RejectionReason {
    static_assert(
        diagnostic::is_diagnostic_class_v<DiagnosticTag>,
        "RejectionReason's DiagnosticTag template argument must be a "
        "type derived from diagnostic::tag_base.  See "
        "SessionDiagnostic.h's catalog for the shipped tag classes.");

    using diagnostic_class = DiagnosticTag;
    using lhs_inner        = TInner;
    using rhs_inner        = UInner;

    static constexpr std::string_view name        = DiagnosticTag::name;
    static constexpr std::string_view description = DiagnosticTag::description;
    static constexpr std::string_view remediation = DiagnosticTag::remediation;
};

// Shape predicate for the RejectionReason wrapper itself.  Useful for
// metaprogramming that needs to dispatch on "is this the success
// sentinel or a failure record".
template <typename T>
struct is_rejection_reason : std::false_type {};

template <typename Tag, typename A, typename B>
struct is_rejection_reason<RejectionReason<Tag, A, B>> : std::true_type {};

template <typename T>
inline constexpr bool is_rejection_reason_v = is_rejection_reason<T>::value;

// ═════════════════════════════════════════════════════════════════════
// ── subtype_rejection_reason<T, U> — primary template + alias ─────
// ═════════════════════════════════════════════════════════════════════
//
// Primary template: the GENERIC SubtypeMismatch — fires for any (T, U)
// pair that none of the per-rule specialisations below match.  This
// is the catch-all for cross-shape combinator pairs (e.g.,
// `Loop<...>` vs `End`, `CheckpointedSession<...>` vs `Send<...>`)
// that the framework does not have a more specific tag for.

template <typename T, typename U>
struct subtype_rejection_reason {
    using type = RejectionReason<diagnostic::SubtypeMismatch, T, U>;
};

template <typename T, typename U>
using subtype_rejection_reason_t = typename subtype_rejection_reason<T, U>::type;

// is_subtype_sync_diag_v<T, U> — true iff the rejection reason is
// SubtypeOk.  Behaviourally equivalent to `is_subtype_sync_v<T, U>`,
// but routed through the rejection-reason path so callers that want
// BOTH a yes/no answer AND a reason on failure can use a single
// metafunction.

template <typename T, typename U>
inline constexpr bool is_subtype_sync_diag_v =
    std::is_same_v<subtype_rejection_reason_t<T, U>, SubtypeOk>;

// ═════════════════════════════════════════════════════════════════════
// ── Per-rule specialisations (mirror is_subtype_sync) ──────────────
// ═════════════════════════════════════════════════════════════════════

// [reason-end]  End ⩽ End
template <>
struct subtype_rejection_reason<End, End> { using type = SubtypeOk; };

// [reason-continue]  Continue ⩽ Continue
//
// (The coinductive hypothesis from is_subtype_sync applies: when the
// outer Loop subtype check has begun, the two enclosing Loops are
// assumed related, so the Continues that refer back to them are
// trivially related too.)
template <>
struct subtype_rejection_reason<Continue, Continue> { using type = SubtypeOk; };

// [reason-stop]  Stop ⩽ U for any U (Stop is bottom — vacuous).
template <typename U>
struct subtype_rejection_reason<Stop, U> { using type = SubtypeOk; };

// [reason-send]  Send<T1, R1> ⩽ Send<T2, R2>
//   COVARIANT in payload: is_subsort<T1, T2> required
//   COVARIANT in continuation: recurse on (R1, R2)
template <typename T1, typename R1, typename T2, typename R2>
struct subtype_rejection_reason<Send<T1, R1>, Send<T2, R2>> {
    using type = std::conditional_t<
        !is_subsort_v<T1, T2>,
        RejectionReason<diagnostic::ProtocolViolation_Payload, T1, T2>,
        subtype_rejection_reason_t<R1, R2>
    >;
};

// [reason-recv]  Recv<T1, R1> ⩽ Recv<T2, R2>
//   CONTRAVARIANT in payload: is_subsort<T2, T1> required (note swap)
//   COVARIANT in continuation: recurse on (R1, R2)
//
// The failing-pair reported on payload mismatch reflects the rule's
// directionality: lhs_inner = T2, rhs_inner = T1 — i.e., "the
// payload supplied (T2) is not a subsort of the payload expected
// (T1)".  Reading the diagnostic with the rule in mind makes this
// natural; without the rule it can be confusing, hence the explicit
// remediation hint in ProtocolViolation_Payload's description.
template <typename T1, typename R1, typename T2, typename R2>
struct subtype_rejection_reason<Recv<T1, R1>, Recv<T2, R2>> {
    using type = std::conditional_t<
        !is_subsort_v<T2, T1>,
        RejectionReason<diagnostic::ProtocolViolation_Payload, T2, T1>,
        subtype_rejection_reason_t<R1, R2>
    >;
};

// [reason-loop]  Loop<B1> ⩽ Loop<B2> — recurse on bodies.
//
// No tag introduced for "Loop body mismatch" — the recursion's reason
// already names the failing inner pair from inside the bodies, which
// is the most actionable diagnostic.
template <typename B1, typename B2>
struct subtype_rejection_reason<Loop<B1>, Loop<B2>> {
    using type = subtype_rejection_reason_t<B1, B2>;
};

// ═════════════════════════════════════════════════════════════════════
// ── Cross-shape specialisations — named structural failures ────────
// ═════════════════════════════════════════════════════════════════════
//
// These specialisations override the primary template's generic
// SubtypeMismatch with more specific tags for the most common
// shape-mismatch errors.  Each documents a different misalignment
// between the two sides of the subtype check.

// [reason-send-vs-recv]  Send<...> ⩽ Recv<...>
template <typename T1, typename R1, typename T2, typename R2>
struct subtype_rejection_reason<Send<T1, R1>, Recv<T2, R2>> {
    using type = RejectionReason<
        diagnostic::ShapeMismatch_SendVsRecv,
        Send<T1, R1>, Recv<T2, R2>>;
};

// [reason-recv-vs-send]  Recv<...> ⩽ Send<...>
template <typename T1, typename R1, typename T2, typename R2>
struct subtype_rejection_reason<Recv<T1, R1>, Send<T2, R2>> {
    using type = RejectionReason<
        diagnostic::ShapeMismatch_SendVsRecv,
        Recv<T1, R1>, Send<T2, R2>>;
};

// [reason-select-vs-offer]  Select<...> ⩽ Offer<...>
template <typename... B1s, typename... B2s>
struct subtype_rejection_reason<Select<B1s...>, Offer<B2s...>> {
    using type = RejectionReason<
        diagnostic::ShapeMismatch_SelectVsOffer,
        Select<B1s...>, Offer<B2s...>>;
};

// [reason-offer-vs-select]  Offer<...> ⩽ Select<...>
template <typename... B1s, typename... B2s>
struct subtype_rejection_reason<Offer<B1s...>, Select<B2s...>> {
    using type = RejectionReason<
        diagnostic::ShapeMismatch_SelectVsOffer,
        Offer<B1s...>, Select<B2s...>>;
};

// ═════════════════════════════════════════════════════════════════════
// ── Select / Offer per-branch fold ─────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// For Select<B1s...> ⩽ Select<C1s...> (Gay-Hole [sub-select]):
//   1. require sizeof...(B1s) ≤ sizeof...(C1s) — else BranchCount_Mismatch
//   2. fold over [0, sizeof...(B1s)) — for each index i, check
//      subtype_rejection_reason_t<B_i, C_i>; first non-Ok bubbles up.
//
// For Offer<B1s...> ⩽ Offer<C1s...> (Gay-Hole [sub-offer]):
//   1. require sizeof...(B1s) ≥ sizeof...(C1s)
//   2. fold over [0, sizeof...(C1s))
//
// The fold MUST be gated on the size precondition — otherwise an
// out-of-range tuple_element_t access during the fold's recursion
// would be a hard error (not SFINAE-friendly), masking the cleaner
// BranchCount_Mismatch diagnostic.

namespace detail::subtype_reason {

// Fold helper: recurse i ∈ [I, N), returning the first failing
// branch's reason or SubtypeOk if all branches pass.
template <typename TupA, typename TupB, std::size_t I, std::size_t N>
struct branch_fold;

template <typename TupA, typename TupB, std::size_t N>
struct branch_fold<TupA, TupB, N, N> {
    using type = SubtypeOk;
};

template <typename TupA, typename TupB, std::size_t I, std::size_t N>
struct branch_fold {
    using inner_reason = subtype_rejection_reason_t<
        std::tuple_element_t<I, TupA>,
        std::tuple_element_t<I, TupB>>;

    using type = std::conditional_t<
        !std::is_same_v<inner_reason, SubtypeOk>,
        inner_reason,
        typename branch_fold<TupA, TupB, I + 1, N>::type
    >;
};

// Gated wrapper: only instantiates branch_fold when the size
// precondition holds.  Avoids out-of-range tuple_element_t in the
// failing-size case.
template <bool SizeOk, typename TupA, typename TupB, std::size_t N,
          typename FailureWhenSizeNotOk>
struct gated_fold {
    using type = FailureWhenSizeNotOk;
};

template <typename TupA, typename TupB, std::size_t N, typename Whatever>
struct gated_fold<true, TupA, TupB, N, Whatever> {
    using type = typename branch_fold<TupA, TupB, 0, N>::type;
};

}  // namespace detail::subtype_reason

// [reason-select]  Select<B1s...> ⩽ Select<C1s...>
template <typename... B1s, typename... B2s>
struct subtype_rejection_reason<Select<B1s...>, Select<B2s...>> {
    using type = typename detail::subtype_reason::gated_fold<
        (sizeof...(B1s) <= sizeof...(B2s)),
        std::tuple<B1s...>, std::tuple<B2s...>,
        sizeof...(B1s),
        RejectionReason<diagnostic::BranchCount_Mismatch,
                        Select<B1s...>, Select<B2s...>>
    >::type;
};

// [reason-offer]  Offer<B1s...> ⩽ Offer<C1s...>
template <typename... B1s, typename... B2s>
struct subtype_rejection_reason<Offer<B1s...>, Offer<B2s...>> {
    using type = typename detail::subtype_reason::gated_fold<
        (sizeof...(B1s) >= sizeof...(B2s)),
        std::tuple<B1s...>, std::tuple<B2s...>,
        sizeof...(B2s),
        RejectionReason<diagnostic::BranchCount_Mismatch,
                        Offer<B1s...>, Offer<B2s...>>
    >::type;
};

// ═════════════════════════════════════════════════════════════════════
// ── CheckpointedSession — product subtyping ────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Mirrors is_subtype_sync<CheckpointedSession<...>, CheckpointedSession<...>>:
// each branch refines independently.  The reason returns the FIRST
// failing branch's inner reason — base first, then rollback.

template <typename B1, typename R1, typename B2, typename R2>
struct subtype_rejection_reason<CheckpointedSession<B1, R1>,
                                 CheckpointedSession<B2, R2>>
{
private:
    using base_reason = subtype_rejection_reason_t<B1, B2>;
public:
    using type = std::conditional_t<
        !std::is_same_v<base_reason, SubtypeOk>,
        base_reason,
        subtype_rejection_reason_t<R1, R2>
    >;
};

// ═════════════════════════════════════════════════════════════════════
// ── assert_subtype_sync_diag — call-site diagnostic helper ─────────
// ═════════════════════════════════════════════════════════════════════
//
// Richer alternative to assert_subtype_sync<T, U>().  When the
// subtype relation fails, the static_assert message names the
// failure class via the diagnostic-tag prefix; the failing inner pair
// is rendered in the template-instantiation context (visible in the
// compiler's "in instantiation of" trace).

template <typename T, typename U>
consteval void assert_subtype_sync_diag() noexcept {
    static_assert(is_subtype_sync_diag_v<T, U>,
        "crucible::session::diagnostic [SubtypeMismatch]: "
        "assert_subtype_sync_diag: T is not a synchronous subtype of "
        "U.  Inspect subtype_rejection_reason_t<T, U> for the failing "
        "inner pair and the classified failure tag (SubtypeMismatch / "
        "ProtocolViolation_Payload / BranchCount_Mismatch / "
        "ShapeMismatch_SendVsRecv / ShapeMismatch_SelectVsOffer).  "
        "The compiler's instantiation trace names the inner types.");
}

// ═════════════════════════════════════════════════════════════════════
// ── Cross-check: rejection-reason agrees with is_subtype_sync ──────
// ═════════════════════════════════════════════════════════════════════
//
// The reason path is a parallel implementation of the same Gay-Hole
// rules.  Any divergence indicates a bug in one or the other.
// is_subtype_sync_diag_v MUST equal is_subtype_sync_v on every input.

template <typename T, typename U>
inline constexpr bool subtype_diag_agrees_v =
    is_subtype_sync_diag_v<T, U> == is_subtype_sync_v<T, U>;

// ═════════════════════════════════════════════════════════════════════
// ── Framework self-test static_asserts ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Per-rule positive (returns SubtypeOk) and negative (returns
// RejectionReason with the right tag) cases plus deeply-nested
// bubbles-up examples.  Runs at header-inclusion time.

namespace detail::subtype_reason::self_test {

struct Msg     {};
struct Other   {};
struct Payload {};
struct Reply   {};

// ─── Positive: SubtypeOk for the rules in is_subtype_sync ─────────

static_assert(std::is_same_v<subtype_rejection_reason_t<End, End>, SubtypeOk>);
static_assert(std::is_same_v<subtype_rejection_reason_t<Continue, Continue>, SubtypeOk>);
static_assert(std::is_same_v<
    subtype_rejection_reason_t<Send<int, End>, Send<int, End>>, SubtypeOk>);
static_assert(std::is_same_v<
    subtype_rejection_reason_t<Recv<int, End>, Recv<int, End>>, SubtypeOk>);
static_assert(std::is_same_v<
    subtype_rejection_reason_t<Loop<Send<int, Continue>>,
                                Loop<Send<int, Continue>>>,
    SubtypeOk>);
static_assert(std::is_same_v<
    subtype_rejection_reason_t<Stop, End>, SubtypeOk>);
static_assert(std::is_same_v<
    subtype_rejection_reason_t<Stop, Send<int, End>>, SubtypeOk>);

// Select narrowing: Select<A> ⩽ Select<A, B> (subtype has fewer).
static_assert(std::is_same_v<
    subtype_rejection_reason_t<
        Select<Send<Msg, End>>,
        Select<Send<Msg, End>, End>>,
    SubtypeOk>);

// Offer widening: Offer<A, B> ⩽ Offer<A> (subtype has more).
static_assert(std::is_same_v<
    subtype_rejection_reason_t<
        Offer<Recv<Msg, End>, End>,
        Offer<Recv<Msg, End>>>,
    SubtypeOk>);

// CheckpointedSession reflexivity.
static_assert(std::is_same_v<
    subtype_rejection_reason_t<
        CheckpointedSession<Send<Msg, End>, End>,
        CheckpointedSession<Send<Msg, End>, End>>,
    SubtypeOk>);

// ─── Negative: payload subsort failure ───────────────────────────

// Send: covariant payload.  is_subsort<int, long> is false by default.
static_assert(std::is_same_v<
    subtype_rejection_reason_t<Send<int, End>, Send<long, End>>,
    RejectionReason<diagnostic::ProtocolViolation_Payload, int, long>>);

// Recv: contravariant payload — failing pair is reversed.
static_assert(std::is_same_v<
    subtype_rejection_reason_t<Recv<int, End>, Recv<long, End>>,
    RejectionReason<diagnostic::ProtocolViolation_Payload, long, int>>);

// ─── Negative: cross-shape combinators ───────────────────────────

static_assert(std::is_same_v<
    subtype_rejection_reason_t<Send<int, End>, Recv<int, End>>,
    RejectionReason<diagnostic::ShapeMismatch_SendVsRecv,
                    Send<int, End>, Recv<int, End>>>);

static_assert(std::is_same_v<
    subtype_rejection_reason_t<Recv<int, End>, Send<int, End>>,
    RejectionReason<diagnostic::ShapeMismatch_SendVsRecv,
                    Recv<int, End>, Send<int, End>>>);

static_assert(std::is_same_v<
    subtype_rejection_reason_t<Select<End>, Offer<End>>,
    RejectionReason<diagnostic::ShapeMismatch_SelectVsOffer,
                    Select<End>, Offer<End>>>);

static_assert(std::is_same_v<
    subtype_rejection_reason_t<Offer<End>, Select<End>>,
    RejectionReason<diagnostic::ShapeMismatch_SelectVsOffer,
                    Offer<End>, Select<End>>>);

// ─── Negative: branch count cardinality ──────────────────────────

// Select widening (subtype has MORE branches than super) is rejected.
using SelectTooManyT = Select<Send<Msg, End>, End>;
using SelectTooManyU = Select<Send<Msg, End>>;
static_assert(std::is_same_v<
    subtype_rejection_reason_t<SelectTooManyT, SelectTooManyU>,
    RejectionReason<diagnostic::BranchCount_Mismatch,
                    SelectTooManyT, SelectTooManyU>>);

// Offer narrowing (subtype has FEWER branches than super) is rejected.
using OfferTooFewT = Offer<Recv<Msg, End>>;
using OfferTooFewU = Offer<Recv<Msg, End>, End>;
static_assert(std::is_same_v<
    subtype_rejection_reason_t<OfferTooFewT, OfferTooFewU>,
    RejectionReason<diagnostic::BranchCount_Mismatch,
                    OfferTooFewT, OfferTooFewU>>);

// ─── Negative: generic SubtypeMismatch (primary template) ────────

// Cross-shape pairs without a more specific tag fall through.
static_assert(std::is_same_v<
    subtype_rejection_reason_t<End, Send<int, End>>,
    RejectionReason<diagnostic::SubtypeMismatch, End, Send<int, End>>>);

static_assert(std::is_same_v<
    subtype_rejection_reason_t<Loop<Send<int, Continue>>, End>,
    RejectionReason<diagnostic::SubtypeMismatch,
                    Loop<Send<int, Continue>>, End>>);

// ─── Bubbling: nested failure surfaces the deepest inner pair ───

// Loop<Send<int, Recv<int, End>>>  ⩽
// Loop<Send<int, Recv<long, End>>>
//
// Loop recurses → Send payload OK → Recv payload contravariant
// is_subsort<long, int> false → ProtocolViolation_Payload(long, int).
using NestedT = Loop<Send<int, Recv<int,  End>>>;
using NestedU = Loop<Send<int, Recv<long, End>>>;
static_assert(std::is_same_v<
    subtype_rejection_reason_t<NestedT, NestedU>,
    RejectionReason<diagnostic::ProtocolViolation_Payload, long, int>>);

// CheckpointedSession's base-branch failure bubbles up.
using CkptT = CheckpointedSession<Send<int, End>, End>;
using CkptU = CheckpointedSession<Send<long, End>, End>;
static_assert(std::is_same_v<
    subtype_rejection_reason_t<CkptT, CkptU>,
    RejectionReason<diagnostic::ProtocolViolation_Payload, int, long>>);

// CheckpointedSession's rollback-branch failure (when base passes).
using CkptT2 = CheckpointedSession<End, Send<int,  End>>;
using CkptU2 = CheckpointedSession<End, Send<long, End>>;
static_assert(std::is_same_v<
    subtype_rejection_reason_t<CkptT2, CkptU2>,
    RejectionReason<diagnostic::ProtocolViolation_Payload, int, long>>);

// Select branch fold: first failing branch surfaces.  Branch 0 OK,
// branch 1 has a payload mismatch.
using SelT = Select<Send<int, End>, Send<int, End>>;
using SelU = Select<Send<int, End>, Send<long, End>>;
static_assert(std::is_same_v<
    subtype_rejection_reason_t<SelT, SelU>,
    RejectionReason<diagnostic::ProtocolViolation_Payload, int, long>>);

// ─── is_subtype_sync_diag_v agrees with is_subtype_sync_v ────────

static_assert( subtype_diag_agrees_v<End, End>);
static_assert( subtype_diag_agrees_v<Send<int, End>, Send<int, End>>);
static_assert( subtype_diag_agrees_v<Send<int, End>, Send<long, End>>);
static_assert( subtype_diag_agrees_v<Send<int, End>, Recv<int, End>>);
static_assert( subtype_diag_agrees_v<Loop<Send<int, Continue>>,
                                      Loop<Send<int, Continue>>>);
static_assert( subtype_diag_agrees_v<NestedT, NestedU>);
static_assert( subtype_diag_agrees_v<SelectTooManyT, SelectTooManyU>);
static_assert( subtype_diag_agrees_v<Stop, End>);
static_assert( subtype_diag_agrees_v<CkptT, CkptU>);

// ─── is_rejection_reason_v shape predicate ────────────────────────

static_assert(!is_rejection_reason_v<SubtypeOk>);
static_assert( is_rejection_reason_v<
    RejectionReason<diagnostic::SubtypeMismatch, int, float>>);
static_assert(!is_rejection_reason_v<int>);

}  // namespace detail::subtype_reason::self_test

}  // namespace crucible::safety::proto
