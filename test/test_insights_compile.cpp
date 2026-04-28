// Sentinel TU for `safety/diag/Insights.h`.
//
// Forces the header's static_asserts AND macro expansions through a
// `.cpp` compilation under the project's full warning flags
// (-Werror=conversion, -Werror=sign-conversion, etc.).  Without this
// TU, the header's invariants are only verified incidentally when
// downstream code happens to include it.
//
// Demonstrates the THREE registration ergonomics offered to end users:
//
//   1. CRUCIBLE_DEFINE_INSIGHTS(Tag, Sev, Why, Symptom, Correct, Violating)
//      — full populate, every field set.
//
//   2. CRUCIBLE_DEFINE_INSIGHTS_SEVERITY(Tag, Sev)
//      — escalate (or pin) severity, leave prose empty.  Tracked-TODO
//        state; `has_insights_v<Tag>` stays FALSE because all four
//        prose fields remain empty.
//
//   3. CRUCIBLE_DEFINE_INSIGHTS_QV(Tag, Sev, Why, Symptom, Correct, Violating)
//      — quality-validated populate; minimum-length asserts catch
//        accidentally-shipped placeholder strings ("TODO").
//
// Plus the two concept gates downstream consumers reach for:
//
//   * WellInsightedTag<T>          — Tag has insights
//   * HasSubstantiveInsights<T>    — insights meet QV thresholds
//
// Mirror of test_diagnostic_compile / test_row_mismatch_compile
// pattern (sentinel TUs that pin header invariants into the build).

#include <crucible/safety/diag/Insights.h>

namespace user_proj::diag_tags {

// ─── User-defined diagnostic tag — must inherit tag_base. ──────────
struct PaymentRefundLeak : ::crucible::safety::diag::tag_base {
    static constexpr std::string_view name        = "PaymentRefundLeak";
    static constexpr std::string_view description =
        "user-defined: payment service refunded a transaction without "
        "rolling back the parent order's revenue accrual";
    static constexpr std::string_view remediation =
        "Use crucible::accounting::TwoPhaseRefund<...> or include the "
        "refund in the original transaction's atomic boundary.";
};

// Shipped-but-prose-pending tag — escalated to Fatal via the
// severity-only macro because it's load-bearing in production.
struct ScheduleDoubleAcceleration : ::crucible::safety::diag::tag_base {
    static constexpr std::string_view name        = "ScheduleDoubleAcceleration";
    static constexpr std::string_view description =
        "user-defined: a scheduled event was accelerated twice in the "
        "same window — surfaces in finance / ops";
    static constexpr std::string_view remediation =
        "Use idempotent acceleration tokens (TODO: extract into combinator).";
};

// QV-validated tag — every prose field clears the default minimum.
struct CrossTenantLeak : ::crucible::safety::diag::tag_base {
    static constexpr std::string_view name        = "CrossTenantLeak";
    static constexpr std::string_view description =
        "user-defined: a query response from tenant A surfaced data "
        "from tenant B's table — multi-tenant isolation breach";
    static constexpr std::string_view remediation =
        "Wrap query handles in TenantScoped<TenantId, T>; refuse "
        "construction across tenant boundaries via Refined<>.";
};

}  // namespace user_proj::diag_tags

// ═════════════════════════════════════════════════════════════════════
// Registration #1 — full insights via CRUCIBLE_DEFINE_INSIGHTS
// ═════════════════════════════════════════════════════════════════════
//
// All five fields populated; the standard registration shape.
CRUCIBLE_DEFINE_INSIGHTS(
    ::user_proj::diag_tags::PaymentRefundLeak,
    ::crucible::safety::diag::Severity::Error,
    "Refunds without parent-order rollback corrupt revenue reporting "
    "and cause month-end reconciliation drift.  The accounting layer's "
    "TwoPhaseRefund<...> primitive is the correct vehicle.",
    "Surfaces as month-end revenue mismatch; the refund line items "
    "exist in the refund ledger but the parent revenue accrual still "
    "shows as recognized.",
    "TwoPhaseRefund<TxId>(parent_id).commit_with_revenue_rollback();",
    "RefundService::refund(parent_id);  // VIOLATES — no rollback");

// ═════════════════════════════════════════════════════════════════════
// Registration #2 — severity-only via CRUCIBLE_DEFINE_INSIGHTS_SEVERITY
// ═════════════════════════════════════════════════════════════════════
//
// Escalation of an existing tag without prose; the prose can be
// added later via CRUCIBLE_DEFINE_INSIGHTS (after removing this
// specialization first — the macro is idempotent over its target).
CRUCIBLE_DEFINE_INSIGHTS_SEVERITY(
    ::user_proj::diag_tags::ScheduleDoubleAcceleration,
    ::crucible::safety::diag::Severity::Fatal);

// ═════════════════════════════════════════════════════════════════════
// Registration #3 — quality-validated via CRUCIBLE_DEFINE_INSIGHTS_QV
// ═════════════════════════════════════════════════════════════════════
//
// Every prose field meets its minimum-length threshold (default:
// why ≥ 30, symptom ≥ 20, correct ≥ 10, violating ≥ 10).  The QV
// variant catches "shipped 'TODO' as the why field" at compile time.
CRUCIBLE_DEFINE_INSIGHTS_QV(
    ::user_proj::diag_tags::CrossTenantLeak,
    ::crucible::safety::diag::Severity::Fatal,
    "A multi-tenant isolation breach is a security incident.  Fixing "
    "after the fact requires customer notification, audit-trail "
    "review, and (depending on jurisdiction) regulatory disclosure.",
    "Surfaces as a query result that includes rows from a different "
    "tenant_id than the requestor's session.",
    "TenantScoped<TenantId, Result> r = query(scope, ...);",
    "Result r = raw_query(...);  // VIOLATES — bypasses TenantScoped");

// ═════════════════════════════════════════════════════════════════════
// Compile-time invariants — the contract this header makes to users.
// ═════════════════════════════════════════════════════════════════════

namespace user_proj::diag_tags::self_test {

namespace diag = ::crucible::safety::diag;

// ── Shape of the registered providers ─────────────────────────────

using Pfull = diag::insight_provider<PaymentRefundLeak>;
static_assert(Pfull::severity == diag::Severity::Error);
static_assert(Pfull::why_this_matters.starts_with("Refunds without"));
static_assert(Pfull::violating_example.find("VIOLATES") != std::string_view::npos);

using Psev = diag::insight_provider<ScheduleDoubleAcceleration>;
static_assert(Psev::severity == diag::Severity::Fatal);
static_assert(Psev::why_this_matters.empty());
static_assert(Psev::correct_example.empty());

using Pqv = diag::insight_provider<CrossTenantLeak>;
static_assert(Pqv::severity == diag::Severity::Fatal);
static_assert(Pqv::why_this_matters.size() >=
              diag::insights_quality_thresholds<CrossTenantLeak>::min_why_chars);
static_assert(Pqv::symptom_pattern.size() >=
              diag::insights_quality_thresholds<CrossTenantLeak>::min_symptom_chars);

// ── has_insights_v semantics ──────────────────────────────────────

// Full registration → has insights.
static_assert(diag::has_insights_v<PaymentRefundLeak>);

// Severity-only registration → does NOT have insights (prose empty).
// This is the "tracked TODO" semantic — the tag's severity is pinned
// but the prose is acknowledged as still-pending.
static_assert(!diag::has_insights_v<ScheduleDoubleAcceleration>);

// QV registration → has insights AND meets QV thresholds.
static_assert(diag::has_insights_v<CrossTenantLeak>);
static_assert(diag::has_substantive_insights_v<CrossTenantLeak>);

// ── Concept gates exercised on user tags ──────────────────────────

static_assert(diag::WellInsightedTag<PaymentRefundLeak>);
static_assert(!diag::WellInsightedTag<ScheduleDoubleAcceleration>);
static_assert(diag::WellInsightedTag<CrossTenantLeak>);

// HasSubstantiveInsights is strictly stronger than WellInsightedTag.
static_assert(!diag::HasSubstantiveInsights<PaymentRefundLeak>
              || diag::has_substantive_insights_v<PaymentRefundLeak>);
static_assert(diag::HasSubstantiveInsights<CrossTenantLeak>);

// Non-tags cannot satisfy WellInsightedTag.
static_assert(!diag::WellInsightedTag<int>);
static_assert(!diag::WellInsightedTag<void>);

}  // namespace user_proj::diag_tags::self_test

// ═════════════════════════════════════════════════════════════════════
// Runtime smoke test — exercises the consteval accessors at runtime
// (forces the template instantiations into the binary).
// ═════════════════════════════════════════════════════════════════════

int main() {
    namespace diag = ::crucible::safety::diag;

    // Severity name accessor (consteval but callable at runtime).
    auto sev_full = diag::insight_provider<
        ::user_proj::diag_tags::PaymentRefundLeak>::severity;
    auto sev_qv = diag::insight_provider<
        ::user_proj::diag_tags::CrossTenantLeak>::severity;

    if (diag::severity_name(sev_full).empty()) return 1;
    if (diag::severity_name(sev_qv).empty())   return 2;

    // The headers' inline runtime smoke tests run via the static
    // initializers when the TU is loaded; reaching main() means none
    // of them aborted.
    return 0;
}
