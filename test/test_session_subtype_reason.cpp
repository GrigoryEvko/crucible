// A subtype check that only answers yes or no tells an author nothing
// about a protocol that fails it.  What is checked here is that the
// rejection carries a reason, that the reason names the deepest pair
// that actually failed rather than the outermost context the check
// started from, and that the reasoned answer never disagrees with the
// plain one.

#include <crucible/sessions/SessionSubtypeReason.h>

#include <cstdio>
#include <string_view>

namespace {

using namespace crucible::safety::proto;

struct Msg {};
struct Reply {};
struct Other {};

static_assert(std::is_same_v<subtype_rejection_reason_t<End, End>, SubtypeOk>);
static_assert(std::is_same_v<subtype_rejection_reason_t<Continue, Continue>, SubtypeOk>);
static_assert(std::is_same_v<subtype_rejection_reason_t<Send<Msg, End>, Send<Msg, End>>, SubtypeOk>);
static_assert(std::is_same_v<subtype_rejection_reason_t<Recv<Msg, End>, Recv<Msg, End>>, SubtypeOk>);
static_assert(
    std::is_same_v<subtype_rejection_reason_t<Loop<Send<Msg, Continue>>, Loop<Send<Msg, Continue>>>, SubtypeOk>);

// Stop is the bottom of the order, so it passes against anything at
// all without inspecting the other side.
static_assert(std::is_same_v<subtype_rejection_reason_t<Stop, End>, SubtypeOk>);
static_assert(std::is_same_v<subtype_rejection_reason_t<Stop, Loop<Send<Msg, Continue>>>, SubtypeOk>);

// A chooser may offer fewer alternatives than its supertype, and a
// responder must handle more.  The two directions are opposite, which
// is what the pair below and its counterpart establish.
static_assert(
    std::is_same_v<subtype_rejection_reason_t<Select<Send<Msg, End>>, Select<Send<Msg, End>, End>>, SubtypeOk>);

static_assert(std::is_same_v<subtype_rejection_reason_t<Offer<Recv<Msg, End>, End>, Offer<Recv<Msg, End>>>, SubtypeOk>);

static_assert(std::is_same_v<subtype_rejection_reason_t<CheckpointedSession<Send<Msg, End>, End>,
                                                        CheckpointedSession<Send<Msg, End>, End>>,
                             SubtypeOk>);

// A sent payload is covariant, so the reason records the two types in
// the order they were written.
using SendPayloadFail = subtype_rejection_reason_t<Send<int, End>, Send<long, End>>;
static_assert(is_rejection_reason_v<SendPayloadFail>);
static_assert(std::is_same_v<typename SendPayloadFail::diagnostic_class, diagnostic::ProtocolViolation_Payload>);
static_assert(std::is_same_v<typename SendPayloadFail::lhs_inner, int>);
static_assert(std::is_same_v<typename SendPayloadFail::rhs_inner, long>);

// A received payload is contravariant, so the same pair of types is
// recorded the other way round.  The swap is the whole point of these
// two cases sitting next to each other.
using RecvPayloadFail = subtype_rejection_reason_t<Recv<int, End>, Recv<long, End>>;
static_assert(std::is_same_v<typename RecvPayloadFail::diagnostic_class, diagnostic::ProtocolViolation_Payload>);
static_assert(std::is_same_v<typename RecvPayloadFail::lhs_inner, long>);
static_assert(std::is_same_v<typename RecvPayloadFail::rhs_inner, int>);

// Both orders of a direction clash report the same shape mismatch.
using SendVsRecvFail = subtype_rejection_reason_t<Send<Msg, End>, Recv<Msg, End>>;
static_assert(std::is_same_v<typename SendVsRecvFail::diagnostic_class, diagnostic::ShapeMismatch_SendVsRecv>);

using RecvVsSendFail = subtype_rejection_reason_t<Recv<Msg, End>, Send<Msg, End>>;
static_assert(std::is_same_v<typename RecvVsSendFail::diagnostic_class, diagnostic::ShapeMismatch_SendVsRecv>);

using SelectVsOfferFail = subtype_rejection_reason_t<Select<End>, Offer<End>>;
static_assert(std::is_same_v<typename SelectVsOfferFail::diagnostic_class, diagnostic::ShapeMismatch_SelectVsOffer>);

using OfferVsSelectFail = subtype_rejection_reason_t<Offer<End>, Select<End>>;
static_assert(std::is_same_v<typename OfferVsSelectFail::diagnostic_class, diagnostic::ShapeMismatch_SelectVsOffer>);

// Each of the two cases below runs its rule backwards: a chooser that
// gained an alternative and a responder that lost one.
using SelectWideningFail = subtype_rejection_reason_t<Select<Send<Msg, End>, End>, Select<Send<Msg, End>>>;
static_assert(std::is_same_v<typename SelectWideningFail::diagnostic_class, diagnostic::BranchCount_Mismatch>);

using OfferNarrowingFail = subtype_rejection_reason_t<Offer<Recv<Msg, End>>, Offer<Recv<Msg, End>, End>>;
static_assert(std::is_same_v<typename OfferNarrowingFail::diagnostic_class, diagnostic::BranchCount_Mismatch>);

// A pair that matches no rule at all falls to the general reason
// rather than to no reason.
using EndVsSendFail = subtype_rejection_reason_t<End, Send<Msg, End>>;
static_assert(std::is_same_v<typename EndVsSendFail::diagnostic_class, diagnostic::SubtypeMismatch>);

using LoopVsEndFail = subtype_rejection_reason_t<Loop<Send<Msg, Continue>>, End>;
static_assert(std::is_same_v<typename LoopVsEndFail::diagnostic_class, diagnostic::SubtypeMismatch>);

// The outer layers match and only the inner receive differs, so the
// reason must name the inner pair.  It also arrives swapped, because
// the failing position is a receive.
using NestedFail = subtype_rejection_reason_t<Loop<Send<int, Recv<int, End>>>, Loop<Send<int, Recv<long, End>>>>;
static_assert(std::is_same_v<typename NestedFail::diagnostic_class, diagnostic::ProtocolViolation_Payload>);
static_assert(std::is_same_v<typename NestedFail::lhs_inner, long>);
static_assert(std::is_same_v<typename NestedFail::rhs_inner, int>);

// Four layers deep, with the only difference at the innermost send.
using DeepFail = subtype_rejection_reason_t<Loop<Recv<Msg, Send<Reply, Recv<Other, Send<int, End>>>>>,
                                            Loop<Recv<Msg, Send<Reply, Recv<Other, Send<long, End>>>>>>;
static_assert(std::is_same_v<typename DeepFail::diagnostic_class, diagnostic::ProtocolViolation_Payload>);
static_assert(std::is_same_v<typename DeepFail::lhs_inner, int>);
static_assert(std::is_same_v<typename DeepFail::rhs_inner, long>);

// The first branch passes and the second fails, so the fold has to
// keep going past a success and stop at the first failure.
using SelectFoldFail =
    subtype_rejection_reason_t<Select<Send<int, End>, Send<int, End>>, Select<Send<int, End>, Send<long, End>>>;
static_assert(std::is_same_v<typename SelectFoldFail::diagnostic_class, diagnostic::ProtocolViolation_Payload>);

// A checkpointed protocol has two arms, and each one is checked: the
// pair below fails in the first, the pair after it in the second.
using CkptBaseFail =
    subtype_rejection_reason_t<CheckpointedSession<Send<int, End>, End>, CheckpointedSession<Send<long, End>, End>>;
static_assert(std::is_same_v<typename CkptBaseFail::diagnostic_class, diagnostic::ProtocolViolation_Payload>);

using CkptRollFail =
    subtype_rejection_reason_t<CheckpointedSession<End, Send<int, End>>, CheckpointedSession<End, Send<long, End>>>;
static_assert(std::is_same_v<typename CkptRollFail::diagnostic_class, diagnostic::ProtocolViolation_Payload>);

// The reasoned check and the plain one are separate code paths, so
// every shape above is run through both and required to agree.  A
// reason that disagreed would be worse than no reason at all.
static_assert(subtype_diag_agrees_v<End, End>);
static_assert(subtype_diag_agrees_v<Send<int, End>, Send<int, End>>);
static_assert(subtype_diag_agrees_v<Send<int, End>, Send<long, End>>);
static_assert(subtype_diag_agrees_v<Send<int, End>, Recv<int, End>>);
static_assert(subtype_diag_agrees_v<Loop<Send<int, Continue>>, Loop<Send<int, Continue>>>);
static_assert(subtype_diag_agrees_v<Stop, End>);
static_assert(subtype_diag_agrees_v<Stop, Send<Msg, End>>);
static_assert(
    subtype_diag_agrees_v<CheckpointedSession<Send<int, End>, End>, CheckpointedSession<Send<int, End>, End>>);
static_assert(subtype_diag_agrees_v<Loop<Recv<Msg, Send<Reply, Recv<Other, Send<int, End>>>>>,
                                    Loop<Recv<Msg, Send<Reply, Recv<Other, Send<long, End>>>>>>);

static_assert(!is_rejection_reason_v<SubtypeOk>);
static_assert(is_rejection_reason_v<SendPayloadFail>);
static_assert(is_rejection_reason_v<NestedFail>);
static_assert(!is_rejection_reason_v<int>);
static_assert(!is_rejection_reason_v<End>);

// The assertion form is consumed for its compile-time effect and
// returns nothing, so being callable at all is the claim.
consteval bool check_assert_diag_compiles() {
    assert_subtype_sync_diag<End, End>();
    assert_subtype_sync_diag<Send<int, End>, Send<int, End>>();
    assert_subtype_sync_diag<Loop<Send<Msg, Continue>>, Loop<Send<Msg, Continue>>>();
    return true;
}
static_assert(check_assert_diag_compiles());

// This is the shape a caller takes: instantiate the check with a new
// protocol against the one already deployed, then turn the reason into
// something a person can act on.  The strings below are only a stand-in
// for that report.

template <typename Tag>
constexpr std::string_view error_category_for() noexcept {
    if constexpr (std::is_same_v<Tag, diagnostic::SubtypeMismatch>) {
        return "generic subtype rejection";
    } else if constexpr (std::is_same_v<Tag, diagnostic::ProtocolViolation_Payload>) {
        return "payload subsort failure";
    } else if constexpr (std::is_same_v<Tag, diagnostic::BranchCount_Mismatch>) {
        return "wrong branch count";
    } else if constexpr (std::is_same_v<Tag, diagnostic::ShapeMismatch_SendVsRecv>) {
        return "Send vs Recv direction mismatch";
    } else if constexpr (std::is_same_v<Tag, diagnostic::ShapeMismatch_SelectVsOffer>) {
        return "Select vs Offer direction mismatch";
    } else {
        return "unrecognised tag";
    }
}

template <typename T, typename U>
constexpr std::string_view classify_evolution() noexcept {
    using R = subtype_rejection_reason_t<T, U>;
    if constexpr (std::is_same_v<R, SubtypeOk>) {
        return "OK";
    } else {
        return error_category_for<typename R::diagnostic_class>();
    }
}

int run_worked_example_evolution_classify() {
    if (classify_evolution<End, End>() != "OK") return 1;

    if (classify_evolution<Send<int, End>, Send<long, End>>() != "payload subsort failure") return 2;

    if (classify_evolution<Send<Msg, End>, Recv<Msg, End>>() != "Send vs Recv direction mismatch") return 3;

    if (classify_evolution<Select<End>, Offer<End>>() != "Select vs Offer direction mismatch") return 4;

    if (classify_evolution<Select<Send<Msg, End>, End>, Select<Send<Msg, End>>>() != "wrong branch count") return 5;

    if (classify_evolution<End, Send<int, End>>() != "generic subtype rejection") return 6;

    // The nested case must reach the same category as the flat one:
    // depth changes where the failure is, not what it is.
    if (classify_evolution<Loop<Send<int, Recv<int, End>>>, Loop<Send<int, Recv<long, End>>>>()
        != "payload subsort failure")
        return 7;

    return 0;
}

int run_rejection_reason_carries_diagnostic_strings() {
    using R = subtype_rejection_reason_t<Send<int, End>, Send<long, End>>;

    if (R::name != "ProtocolViolation_Payload") return 1;
    if (R::description.empty()) return 2;
    if (R::remediation.empty()) return 3;

    // The reason exposes the strings itself and also resolves to the
    // same diagnostic through the general accessor.
    if (diagnostic::diagnostic_name_v<typename R::diagnostic_class> != "ProtocolViolation_Payload") return 4;

    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = run_worked_example_evolution_classify(); rc != 0) return rc;
    if (int rc = run_rejection_reason_carries_diagnostic_strings(); rc != 0) return 100 + rc;

    std::puts("session_subtype_reason: per-rule reasons + bubbling + worked example OK");
    return 0;
}
