// Runtime + compile-time harness for safety/SessionSubtypeReason.h
// (task #380, SEPLOG-STRUCT-6).
//
// Coverage:
//   * Compile-time: subtype_rejection_reason_t maps every successful
//     Gay-Hole rule to SubtypeOk and every failure mode to a
//     RejectionReason tagged with the right diagnostic class.
//   * Compile-time: is_subtype_sync_diag_v tracks is_subtype_sync_v
//     on every input (cross-check via subtype_diag_agrees_v).
//   * Compile-time: deeply-nested rejections bubble the FIRST
//     failing inner pair through Loop / Select / Offer /
//     CheckpointedSession layers — the diagnostic names the
//     deepest failing point, not the outermost typing context.
//   * Compile-time: is_rejection_reason_v shape predicate.
//   * Runtime smoke: a worked "protocol-evolution check" pattern
//     where the rejection reason's tag drives a structured error
//     report.

#include <crucible/sessions/SessionSubtypeReason.h>

#include <cstdio>
#include <string_view>

namespace {

using namespace crucible::safety::proto;

// ── Fixture types ──────────────────────────────────────────────────

struct Msg     {};
struct Reply   {};
struct Other   {};

// ── Compile-time: per-rule positive cases (mirrored TU-side for
//    regression visibility) ────────────────────────────────────────

static_assert(std::is_same_v<
    subtype_rejection_reason_t<End, End>, SubtypeOk>);
static_assert(std::is_same_v<
    subtype_rejection_reason_t<Continue, Continue>, SubtypeOk>);
static_assert(std::is_same_v<
    subtype_rejection_reason_t<Send<Msg, End>, Send<Msg, End>>, SubtypeOk>);
static_assert(std::is_same_v<
    subtype_rejection_reason_t<Recv<Msg, End>, Recv<Msg, End>>, SubtypeOk>);
static_assert(std::is_same_v<
    subtype_rejection_reason_t<Loop<Send<Msg, Continue>>,
                                Loop<Send<Msg, Continue>>>, SubtypeOk>);

// Stop is bottom — vacuously a subtype of any U.
static_assert(std::is_same_v<
    subtype_rejection_reason_t<Stop, End>, SubtypeOk>);
static_assert(std::is_same_v<
    subtype_rejection_reason_t<Stop, Loop<Send<Msg, Continue>>>, SubtypeOk>);

// Select narrowing (subtype picks fewer); Offer widening (subtype
// handles more).
static_assert(std::is_same_v<
    subtype_rejection_reason_t<
        Select<Send<Msg, End>>,
        Select<Send<Msg, End>, End>>,
    SubtypeOk>);

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

// ── Compile-time: per-rule failure cases with right tag ───────────

// Send payload (covariant): is_subsort<int, long> is false → tagged
// ProtocolViolation_Payload(int, long) (LHS-payload, RHS-payload).
using SendPayloadFail =
    subtype_rejection_reason_t<Send<int, End>, Send<long, End>>;
static_assert(is_rejection_reason_v<SendPayloadFail>);
static_assert(std::is_same_v<typename SendPayloadFail::diagnostic_class,
                              diagnostic::ProtocolViolation_Payload>);
static_assert(std::is_same_v<typename SendPayloadFail::lhs_inner, int>);
static_assert(std::is_same_v<typename SendPayloadFail::rhs_inner, long>);

// Recv payload (CONTRAVARIANT): is_subsort<long, int> false → tagged
// ProtocolViolation_Payload(long, int).  Note the swap.
using RecvPayloadFail =
    subtype_rejection_reason_t<Recv<int, End>, Recv<long, End>>;
static_assert(std::is_same_v<typename RecvPayloadFail::diagnostic_class,
                              diagnostic::ProtocolViolation_Payload>);
static_assert(std::is_same_v<typename RecvPayloadFail::lhs_inner, long>);
static_assert(std::is_same_v<typename RecvPayloadFail::rhs_inner, int>);

// Send vs Recv → ShapeMismatch_SendVsRecv.
using SendVsRecvFail =
    subtype_rejection_reason_t<Send<Msg, End>, Recv<Msg, End>>;
static_assert(std::is_same_v<typename SendVsRecvFail::diagnostic_class,
                              diagnostic::ShapeMismatch_SendVsRecv>);

// Recv vs Send → ShapeMismatch_SendVsRecv (symmetric).
using RecvVsSendFail =
    subtype_rejection_reason_t<Recv<Msg, End>, Send<Msg, End>>;
static_assert(std::is_same_v<typename RecvVsSendFail::diagnostic_class,
                              diagnostic::ShapeMismatch_SendVsRecv>);

// Select vs Offer → ShapeMismatch_SelectVsOffer.
using SelectVsOfferFail =
    subtype_rejection_reason_t<Select<End>, Offer<End>>;
static_assert(std::is_same_v<typename SelectVsOfferFail::diagnostic_class,
                              diagnostic::ShapeMismatch_SelectVsOffer>);

// Offer vs Select → ShapeMismatch_SelectVsOffer (symmetric).
using OfferVsSelectFail =
    subtype_rejection_reason_t<Offer<End>, Select<End>>;
static_assert(std::is_same_v<typename OfferVsSelectFail::diagnostic_class,
                              diagnostic::ShapeMismatch_SelectVsOffer>);

// Select widening (subtype has more branches than super) — wrong
// direction → BranchCount_Mismatch.
using SelectWideningFail = subtype_rejection_reason_t<
    Select<Send<Msg, End>, End>,
    Select<Send<Msg, End>>>;
static_assert(std::is_same_v<typename SelectWideningFail::diagnostic_class,
                              diagnostic::BranchCount_Mismatch>);

// Offer narrowing (subtype has fewer branches than super) — wrong
// direction → BranchCount_Mismatch.
using OfferNarrowingFail = subtype_rejection_reason_t<
    Offer<Recv<Msg, End>>,
    Offer<Recv<Msg, End>, End>>;
static_assert(std::is_same_v<typename OfferNarrowingFail::diagnostic_class,
                              diagnostic::BranchCount_Mismatch>);

// Generic SubtypeMismatch (primary-template catchall).
using EndVsSendFail = subtype_rejection_reason_t<End, Send<Msg, End>>;
static_assert(std::is_same_v<typename EndVsSendFail::diagnostic_class,
                              diagnostic::SubtypeMismatch>);

using LoopVsEndFail = subtype_rejection_reason_t<
    Loop<Send<Msg, Continue>>, End>;
static_assert(std::is_same_v<typename LoopVsEndFail::diagnostic_class,
                              diagnostic::SubtypeMismatch>);

// ── Compile-time: deeply-nested bubbling — surface deepest failure
//    pair, not the outermost typing context ─────────────────────────

// Loop<Send<int, Recv<int, End>>>  vs  Loop<Send<int, Recv<long, End>>>:
// Loop ⩽ Loop recurses → Send payload OK → Recv payload contravariant
// is_subsort<long, int> false → ProtocolViolation_Payload(long, int).
using NestedFail = subtype_rejection_reason_t<
    Loop<Send<int, Recv<int,  End>>>,
    Loop<Send<int, Recv<long, End>>>>;
static_assert(std::is_same_v<typename NestedFail::diagnostic_class,
                              diagnostic::ProtocolViolation_Payload>);
static_assert(std::is_same_v<typename NestedFail::lhs_inner, long>);
static_assert(std::is_same_v<typename NestedFail::rhs_inner, int>);

// Even deeper: 4-level nest with the failure at the innermost Send.
using DeepFail = subtype_rejection_reason_t<
    Loop<Recv<Msg, Send<Reply, Recv<Other, Send<int,  End>>>>>,
    Loop<Recv<Msg, Send<Reply, Recv<Other, Send<long, End>>>>>>;
static_assert(std::is_same_v<typename DeepFail::diagnostic_class,
                              diagnostic::ProtocolViolation_Payload>);
static_assert(std::is_same_v<typename DeepFail::lhs_inner, int>);
static_assert(std::is_same_v<typename DeepFail::rhs_inner, long>);

// Select branch fold: first failing branch surfaces.  Branch 0 OK
// (int ⩽ int), branch 1 fails (int vs long).
using SelectFoldFail = subtype_rejection_reason_t<
    Select<Send<int, End>, Send<int, End>>,
    Select<Send<int, End>, Send<long, End>>>;
static_assert(std::is_same_v<typename SelectFoldFail::diagnostic_class,
                              diagnostic::ProtocolViolation_Payload>);

// CheckpointedSession base-branch failure bubbles up.
using CkptBaseFail = subtype_rejection_reason_t<
    CheckpointedSession<Send<int, End>, End>,
    CheckpointedSession<Send<long, End>, End>>;
static_assert(std::is_same_v<typename CkptBaseFail::diagnostic_class,
                              diagnostic::ProtocolViolation_Payload>);

// CheckpointedSession rollback-branch failure (when base passes).
using CkptRollFail = subtype_rejection_reason_t<
    CheckpointedSession<End, Send<int,  End>>,
    CheckpointedSession<End, Send<long, End>>>;
static_assert(std::is_same_v<typename CkptRollFail::diagnostic_class,
                              diagnostic::ProtocolViolation_Payload>);

// ── Compile-time: is_subtype_sync_diag_v agrees with
//    is_subtype_sync_v on every input ─────────────────────────────

static_assert( subtype_diag_agrees_v<End, End>);
static_assert( subtype_diag_agrees_v<Send<int, End>, Send<int, End>>);
static_assert( subtype_diag_agrees_v<Send<int, End>, Send<long, End>>);
static_assert( subtype_diag_agrees_v<Send<int, End>, Recv<int, End>>);
static_assert( subtype_diag_agrees_v<Loop<Send<int, Continue>>,
                                      Loop<Send<int, Continue>>>);
static_assert( subtype_diag_agrees_v<Stop, End>);
static_assert( subtype_diag_agrees_v<Stop, Send<Msg, End>>);
static_assert( subtype_diag_agrees_v<
    CheckpointedSession<Send<int, End>, End>,
    CheckpointedSession<Send<int, End>, End>>);
static_assert( subtype_diag_agrees_v<
    Loop<Recv<Msg, Send<Reply, Recv<Other, Send<int, End>>>>>,
    Loop<Recv<Msg, Send<Reply, Recv<Other, Send<long, End>>>>>>);

// ── Compile-time: is_rejection_reason_v shape predicate ─────────

static_assert(!is_rejection_reason_v<SubtypeOk>);
static_assert( is_rejection_reason_v<SendPayloadFail>);
static_assert( is_rejection_reason_v<NestedFail>);
static_assert(!is_rejection_reason_v<int>);
static_assert(!is_rejection_reason_v<End>);

// ── Compile-time: assert_subtype_sync_diag<T, T>() compiles for
//    reflexive cases ───────────────────────────────────────────────

consteval bool check_assert_diag_compiles() {
    assert_subtype_sync_diag<End, End>();
    assert_subtype_sync_diag<Send<int, End>, Send<int, End>>();
    assert_subtype_sync_diag<
        Loop<Send<Msg, Continue>>,
        Loop<Send<Msg, Continue>>>();
    return true;
}
static_assert(check_assert_diag_compiles());

// ── Worked example: protocol-evolution check that drives a
//    structured error report from the rejection-reason tag ─────────
//
// In production, a Vessel adapter would instantiate this template
// with (NewProto, OldProto) at startup.  The runtime path here just
// confirms the tag-based dispatch composes — the payload is a
// human-readable error category derived purely from the tag.

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
    // Reflexive: OK.
    if (classify_evolution<End, End>() != "OK") return 1;

    // Send payload mismatch.
    if (classify_evolution<Send<int, End>, Send<long, End>>()
        != "payload subsort failure") return 2;

    // Send vs Recv direction.
    if (classify_evolution<Send<Msg, End>, Recv<Msg, End>>()
        != "Send vs Recv direction mismatch") return 3;

    // Select vs Offer direction.
    if (classify_evolution<Select<End>, Offer<End>>()
        != "Select vs Offer direction mismatch") return 4;

    // Branch count cardinality.
    if (classify_evolution<
            Select<Send<Msg, End>, End>,
            Select<Send<Msg, End>>>()
        != "wrong branch count") return 5;

    // Generic catchall.
    if (classify_evolution<End, Send<int, End>>()
        != "generic subtype rejection") return 6;

    // Nested: bubbles up the deepest payload failure.
    if (classify_evolution<
            Loop<Send<int, Recv<int,  End>>>,
            Loop<Send<int, Recv<long, End>>>>()
        != "payload subsort failure") return 7;

    return 0;
}

// ── Runtime: RejectionReason carries the diagnostic's strings ────

int run_rejection_reason_carries_diagnostic_strings() {
    using R = subtype_rejection_reason_t<Send<int, End>, Send<long, End>>;

    if (R::name        != "ProtocolViolation_Payload") return 1;
    if (R::description.empty())                         return 2;
    if (R::remediation.empty())                         return 3;

    // Composes with diagnostic accessors directly.
    if (diagnostic::diagnostic_name_v<typename R::diagnostic_class>
        != "ProtocolViolation_Payload") return 4;

    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = run_worked_example_evolution_classify();           rc != 0) return rc;
    if (int rc = run_rejection_reason_carries_diagnostic_strings(); rc != 0) return 100 + rc;

    std::puts("session_subtype_reason: per-rule reasons + bubbling + worked example OK");
    return 0;
}
