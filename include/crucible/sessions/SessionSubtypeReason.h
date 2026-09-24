#pragma once

// A companion to the subtype relation that answers why a check failed
// rather than only whether it did.  The walk runs in lockstep with the
// relation itself, stops at the first failing pair, and names that pair
// alongside a class for the failure.
//
// Only a shape mismatch, a payload mismatch and a branch-count mismatch
// get a class of their own.  A failure inside a continuation, a loop
// body or a branch keeps the inner pair's reason and passes it outward
// unchanged, so the caller receives the innermost cause rather than the
// outermost shape.

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCheckpoint.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionDiagnostic.h>
#include <crucible/sessions/SessionSubtype.h>

#include <cstddef>
#include <tuple>
#include <type_traits>

namespace crucible::safety::proto {

struct SubtypeOk {};

template <typename DiagnosticTag, typename TInner, typename UInner>
struct RejectionReason {
    static_assert(diagnostic::is_diagnostic_class_v<DiagnosticTag>,
                  "RejectionReason's DiagnosticTag template argument must be a "
                  "type derived from diagnostic::tag_base.  Pick one of the "
                  "classes in the diagnostic catalog.");

    using diagnostic_class = DiagnosticTag;
    using lhs_inner = TInner;
    using rhs_inner = UInner;

    static constexpr std::string_view name = DiagnosticTag::name;
    static constexpr std::string_view description = DiagnosticTag::description;
    static constexpr std::string_view remediation = DiagnosticTag::remediation;
};

template <typename T>
struct is_rejection_reason : std::false_type {};

template <typename Tag, typename A, typename B>
struct is_rejection_reason<RejectionReason<Tag, A, B>> : std::true_type {};

template <typename T>
inline constexpr bool is_rejection_reason_v = is_rejection_reason<T>::value;

template <typename T, typename U>
struct subtype_rejection_reason {
    using type = RejectionReason<diagnostic::SubtypeMismatch, T, U>;
};

template <typename T, typename U>
using subtype_rejection_reason_t = typename subtype_rejection_reason<T, U>::type;

template <typename T, typename U>
inline constexpr bool is_subtype_sync_diag_v = std::is_same_v<subtype_rejection_reason_t<T, U>, SubtypeOk>;

template <>
struct subtype_rejection_reason<End, End> {
    using type = SubtypeOk;
};

// Reaching a loop-closing step on both sides means the enclosing loops
// are already being compared, so the two jumps back to them are related
// by that same assumption and need no check of their own.
template <>
struct subtype_rejection_reason<Continue, Continue> {
    using type = SubtypeOk;
};

// A crashed endpoint sits below every protocol, so no counterpart can
// reject it.
template <typename U>
struct subtype_rejection_reason<Stop, U> {
    using type = SubtypeOk;
};

template <typename T1, typename R1, typename T2, typename R2>
struct subtype_rejection_reason<Send<T1, R1>, Send<T2, R2>> {
    using type =
        std::conditional_t<!is_subsort_v<T1, T2>, RejectionReason<diagnostic::ProtocolViolation_Payload, T1, T2>,
                           subtype_rejection_reason_t<R1, R2>>;
};

// A receive payload runs against the direction of the check, so the
// pair reported here is swapped relative to the operands: it reads as
// "the payload supplied is not a subsort of the payload expected".
// Without the rule in mind that ordering looks inverted.
template <typename T1, typename R1, typename T2, typename R2>
struct subtype_rejection_reason<Recv<T1, R1>, Recv<T2, R2>> {
    using type =
        std::conditional_t<!is_subsort_v<T2, T1>, RejectionReason<diagnostic::ProtocolViolation_Payload, T2, T1>,
                           subtype_rejection_reason_t<R1, R2>>;
};

template <typename B1, typename B2>
struct subtype_rejection_reason<Loop<B1>, Loop<B2>> {
    using type = subtype_rejection_reason_t<B1, B2>;
};

template <typename T1, typename R1, typename T2, typename R2>
struct subtype_rejection_reason<Send<T1, R1>, Recv<T2, R2>> {
    using type = RejectionReason<diagnostic::ShapeMismatch_SendVsRecv, Send<T1, R1>, Recv<T2, R2>>;
};

template <typename T1, typename R1, typename T2, typename R2>
struct subtype_rejection_reason<Recv<T1, R1>, Send<T2, R2>> {
    using type = RejectionReason<diagnostic::ShapeMismatch_SendVsRecv, Recv<T1, R1>, Send<T2, R2>>;
};

template <typename... B1s, typename... B2s>
struct subtype_rejection_reason<Select<B1s...>, Offer<B2s...>> {
    using type = RejectionReason<diagnostic::ShapeMismatch_SelectVsOffer, Select<B1s...>, Offer<B2s...>>;
};

template <typename... B1s, typename... B2s>
struct subtype_rejection_reason<Offer<B1s...>, Select<B2s...>> {
    using type = RejectionReason<diagnostic::ShapeMismatch_SelectVsOffer, Offer<B1s...>, Select<B2s...>>;
};

// Branch counts move in opposite directions for the two choice
// combinators.  The side that picks may offer fewer alternatives than
// the position expects, since it simply never takes the missing ones.
// The side that is picked from must handle at least every alternative
// the position can send it.  So a Select subtype carries no more
// branches than its supertype and an Offer subtype no fewer, and each
// walk covers only the branches both sides have.

namespace detail::subtype_reason {

template <typename TupA, typename TupB, std::size_t I, std::size_t N>
struct branch_fold;

template <typename TupA, typename TupB, std::size_t N>
struct branch_fold<TupA, TupB, N, N> {
    using type = SubtypeOk;
};

template <typename TupA, typename TupB, std::size_t I, std::size_t N>
struct branch_fold {
    using inner_reason = subtype_rejection_reason_t<std::tuple_element_t<I, TupA>, std::tuple_element_t<I, TupB>>;

    using type = std::conditional_t<!std::is_same_v<inner_reason, SubtypeOk>, inner_reason,
                                    typename branch_fold<TupA, TupB, I + 1, N>::type>;
};

// The fold has to stay behind the count check.  An index past the end
// of either pack is a hard error rather than a failed substitution, so
// running the fold first would replace the branch-count report with an
// unrelated compiler error.
template <bool SizeOk, typename TupA, typename TupB, std::size_t N, typename FailureWhenSizeNotOk>
struct gated_fold {
    using type = FailureWhenSizeNotOk;
};

template <typename TupA, typename TupB, std::size_t N, typename Whatever>
struct gated_fold<true, TupA, TupB, N, Whatever> {
    using type = typename branch_fold<TupA, TupB, 0, N>::type;
};

}  // namespace detail::subtype_reason

template <typename... B1s, typename... B2s>
struct subtype_rejection_reason<Select<B1s...>, Select<B2s...>> {
    using type = typename detail::subtype_reason::gated_fold<
        (sizeof...(B1s) <= sizeof...(B2s)), std::tuple<B1s...>, std::tuple<B2s...>, sizeof...(B1s),
        RejectionReason<diagnostic::BranchCount_Mismatch, Select<B1s...>, Select<B2s...>>>::type;
};

template <typename... B1s, typename... B2s>
struct subtype_rejection_reason<Offer<B1s...>, Offer<B2s...>> {
    using type = typename detail::subtype_reason::gated_fold<
        (sizeof...(B1s) >= sizeof...(B2s)), std::tuple<B1s...>, std::tuple<B2s...>, sizeof...(B2s),
        RejectionReason<diagnostic::BranchCount_Mismatch, Offer<B1s...>, Offer<B2s...>>>::type;
};

template <typename B1, typename R1, typename B2, typename R2>
struct subtype_rejection_reason<CheckpointedSession<B1, R1>, CheckpointedSession<B2, R2>> {
private:
    using base_reason = subtype_rejection_reason_t<B1, B2>;

public:
    using type =
        std::conditional_t<!std::is_same_v<base_reason, SubtypeOk>, base_reason, subtype_rejection_reason_t<R1, R2>>;
};

// The message carries the failure class.  The failing pair itself
// appears in the compiler's instantiation trace rather than the text.

template <typename T, typename U>
consteval void assert_subtype_sync_diag() noexcept {
    static_assert(is_subtype_sync_diag_v<T, U>, "crucible::session::diagnostic [SubtypeMismatch]: "
                                                "assert_subtype_sync_diag: T is not a synchronous subtype of "
                                                "U.  Inspect subtype_rejection_reason_t<T, U> for the failing "
                                                "inner pair and the classified failure tag (SubtypeMismatch / "
                                                "ProtocolViolation_Payload / BranchCount_Mismatch / "
                                                "ShapeMismatch_SendVsRecv / ShapeMismatch_SelectVsOffer).  "
                                                "The compiler's instantiation trace names the inner types.");
}

// The walk here restates the subtype rules a second time, so the two
// can drift apart.  This must hold for every pair of protocols, and a
// case where it does not is a defect in one of the two.

template <typename T, typename U>
inline constexpr bool subtype_diag_agrees_v = is_subtype_sync_diag_v<T, U> == is_subtype_sync_v<T, U>;

namespace detail::subtype_reason::self_test {

struct Msg {};
struct Other {};
struct Payload {};
struct Reply {};

static_assert(std::is_same_v<subtype_rejection_reason_t<End, End>, SubtypeOk>);
static_assert(std::is_same_v<subtype_rejection_reason_t<Continue, Continue>, SubtypeOk>);
static_assert(std::is_same_v<subtype_rejection_reason_t<Send<int, End>, Send<int, End>>, SubtypeOk>);
static_assert(std::is_same_v<subtype_rejection_reason_t<Recv<int, End>, Recv<int, End>>, SubtypeOk>);
static_assert(
    std::is_same_v<subtype_rejection_reason_t<Loop<Send<int, Continue>>, Loop<Send<int, Continue>>>, SubtypeOk>);
static_assert(std::is_same_v<subtype_rejection_reason_t<Stop, End>, SubtypeOk>);
static_assert(std::is_same_v<subtype_rejection_reason_t<Stop, Send<int, End>>, SubtypeOk>);

static_assert(
    std::is_same_v<subtype_rejection_reason_t<Select<Send<Msg, End>>, Select<Send<Msg, End>, End>>, SubtypeOk>);

static_assert(std::is_same_v<subtype_rejection_reason_t<Offer<Recv<Msg, End>, End>, Offer<Recv<Msg, End>>>, SubtypeOk>);

static_assert(std::is_same_v<subtype_rejection_reason_t<CheckpointedSession<Send<Msg, End>, End>,
                                                        CheckpointedSession<Send<Msg, End>, End>>,
                             SubtypeOk>);

// No subsort relation is installed between int and long, so these two
// payloads are unrelated in either direction.
static_assert(std::is_same_v<subtype_rejection_reason_t<Send<int, End>, Send<long, End>>,
                             RejectionReason<diagnostic::ProtocolViolation_Payload, int, long>>);

static_assert(std::is_same_v<subtype_rejection_reason_t<Recv<int, End>, Recv<long, End>>,
                             RejectionReason<diagnostic::ProtocolViolation_Payload, long, int>>);

static_assert(std::is_same_v<subtype_rejection_reason_t<Send<int, End>, Recv<int, End>>,
                             RejectionReason<diagnostic::ShapeMismatch_SendVsRecv, Send<int, End>, Recv<int, End>>>);

static_assert(std::is_same_v<subtype_rejection_reason_t<Recv<int, End>, Send<int, End>>,
                             RejectionReason<diagnostic::ShapeMismatch_SendVsRecv, Recv<int, End>, Send<int, End>>>);

static_assert(std::is_same_v<subtype_rejection_reason_t<Select<End>, Offer<End>>,
                             RejectionReason<diagnostic::ShapeMismatch_SelectVsOffer, Select<End>, Offer<End>>>);

static_assert(std::is_same_v<subtype_rejection_reason_t<Offer<End>, Select<End>>,
                             RejectionReason<diagnostic::ShapeMismatch_SelectVsOffer, Offer<End>, Select<End>>>);

using SelectTooManyT = Select<Send<Msg, End>, End>;
using SelectTooManyU = Select<Send<Msg, End>>;
static_assert(std::is_same_v<subtype_rejection_reason_t<SelectTooManyT, SelectTooManyU>,
                             RejectionReason<diagnostic::BranchCount_Mismatch, SelectTooManyT, SelectTooManyU>>);

using OfferTooFewT = Offer<Recv<Msg, End>>;
using OfferTooFewU = Offer<Recv<Msg, End>, End>;
static_assert(std::is_same_v<subtype_rejection_reason_t<OfferTooFewT, OfferTooFewU>,
                             RejectionReason<diagnostic::BranchCount_Mismatch, OfferTooFewT, OfferTooFewU>>);

static_assert(std::is_same_v<subtype_rejection_reason_t<End, Send<int, End>>,
                             RejectionReason<diagnostic::SubtypeMismatch, End, Send<int, End>>>);

static_assert(std::is_same_v<subtype_rejection_reason_t<Loop<Send<int, Continue>>, End>,
                             RejectionReason<diagnostic::SubtypeMismatch, Loop<Send<int, Continue>>, End>>);

using NestedT = Loop<Send<int, Recv<int, End>>>;
using NestedU = Loop<Send<int, Recv<long, End>>>;
static_assert(std::is_same_v<subtype_rejection_reason_t<NestedT, NestedU>,
                             RejectionReason<diagnostic::ProtocolViolation_Payload, long, int>>);

using CkptT = CheckpointedSession<Send<int, End>, End>;
using CkptU = CheckpointedSession<Send<long, End>, End>;
static_assert(std::is_same_v<subtype_rejection_reason_t<CkptT, CkptU>,
                             RejectionReason<diagnostic::ProtocolViolation_Payload, int, long>>);

using CkptT2 = CheckpointedSession<End, Send<int, End>>;
using CkptU2 = CheckpointedSession<End, Send<long, End>>;
static_assert(std::is_same_v<subtype_rejection_reason_t<CkptT2, CkptU2>,
                             RejectionReason<diagnostic::ProtocolViolation_Payload, int, long>>);

using SelT = Select<Send<int, End>, Send<int, End>>;
using SelU = Select<Send<int, End>, Send<long, End>>;
static_assert(std::is_same_v<subtype_rejection_reason_t<SelT, SelU>,
                             RejectionReason<diagnostic::ProtocolViolation_Payload, int, long>>);

static_assert(subtype_diag_agrees_v<End, End>);
static_assert(subtype_diag_agrees_v<Send<int, End>, Send<int, End>>);
static_assert(subtype_diag_agrees_v<Send<int, End>, Send<long, End>>);
static_assert(subtype_diag_agrees_v<Send<int, End>, Recv<int, End>>);
static_assert(subtype_diag_agrees_v<Loop<Send<int, Continue>>, Loop<Send<int, Continue>>>);
static_assert(subtype_diag_agrees_v<NestedT, NestedU>);
static_assert(subtype_diag_agrees_v<SelectTooManyT, SelectTooManyU>);
static_assert(subtype_diag_agrees_v<Stop, End>);
static_assert(subtype_diag_agrees_v<CkptT, CkptU>);

static_assert(!is_rejection_reason_v<SubtypeOk>);
static_assert(is_rejection_reason_v<RejectionReason<diagnostic::SubtypeMismatch, int, float>>);
static_assert(!is_rejection_reason_v<int>);

}  // namespace detail::subtype_reason::self_test

}  // namespace crucible::safety::proto
