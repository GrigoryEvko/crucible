// The header's assertions and macro expansions are only checked against the
// project's warning flags when some translation unit compiles them. This one
// does that, and exercises all three registration forms while it is at it.

#include <crucible/safety/diag/Insights.h>

namespace user_proj::diag_tags {

// A tag must inherit tag_base to be registrable.
struct PaymentRefundLeak : ::crucible::safety::diag::tag_base {
    static constexpr std::string_view name = "PaymentRefundLeak";
    static constexpr std::string_view description = "user-defined: payment service refunded a transaction without "
                                                    "rolling back the parent order's revenue accrual";
    static constexpr std::string_view remediation = "Use crucible::accounting::TwoPhaseRefund<...> or include the "
                                                    "refund in the original transaction's atomic boundary.";
};

// A tag whose prose is still pending but whose severity already matters, so
// it is registered through the severity-only form.
struct ScheduleDoubleAcceleration : ::crucible::safety::diag::tag_base {
    static constexpr std::string_view name = "ScheduleDoubleAcceleration";
    static constexpr std::string_view description = "user-defined: a scheduled event was accelerated twice in the "
                                                    "same window — surfaces in finance / ops";
    static constexpr std::string_view remediation =
        "Use idempotent acceleration tokens (TODO: extract into combinator).";
};

// A tag whose every prose field clears the quality minimums.
struct CrossTenantLeak : ::crucible::safety::diag::tag_base {
    static constexpr std::string_view name = "CrossTenantLeak";
    static constexpr std::string_view description = "user-defined: a query response from tenant A surfaced data "
                                                    "from tenant B's table — multi-tenant isolation breach";
    static constexpr std::string_view remediation = "Wrap query handles in TenantScoped<TenantId, T>; refuse "
                                                    "construction across tenant boundaries via Refined<>.";
};

}  // namespace user_proj::diag_tags

// Every field populated, which is the ordinary registration shape.
CRUCIBLE_DEFINE_INSIGHTS(::user_proj::diag_tags::PaymentRefundLeak, ::crucible::safety::diag::Severity::Error,
                         "Refunds without parent-order rollback corrupt revenue reporting "
                         "and cause month-end reconciliation drift.  The accounting layer's "
                         "TwoPhaseRefund<...> primitive is the correct vehicle.",
                         "Surfaces as month-end revenue mismatch; the refund line items "
                         "exist in the refund ledger but the parent revenue accrual still "
                         "shows as recognized.",
                         "TwoPhaseRefund<TxId>(parent_id).commit_with_revenue_rollback();",
                         "RefundService::refund(parent_id);  // VIOLATES — no rollback");

// Severity without prose. Adding the prose later means replacing this
// specialization rather than writing a second one alongside it.
CRUCIBLE_DEFINE_INSIGHTS_SEVERITY(::user_proj::diag_tags::ScheduleDoubleAcceleration,
                                  ::crucible::safety::diag::Severity::Fatal);

// The quality-validated form holds each prose field to a minimum length: 30
// characters for the reason, 20 for the symptom, 10 for each example. A
// placeholder left in by accident fails to compile.
CRUCIBLE_DEFINE_INSIGHTS_QV(::user_proj::diag_tags::CrossTenantLeak, ::crucible::safety::diag::Severity::Fatal,
                            "A multi-tenant isolation breach is a security incident.  Fixing "
                            "after the fact requires customer notification, audit-trail "
                            "review, and (depending on jurisdiction) regulatory disclosure.",
                            "Surfaces as a query result that includes rows from a different "
                            "tenant_id than the requestor's session.",
                            "TenantScoped<TenantId, Result> r = query(scope, ...);",
                            "Result r = raw_query(...);  // VIOLATES — bypasses TenantScoped");

namespace user_proj::diag_tags::self_test {

namespace diag = ::crucible::safety::diag;

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
static_assert(Pqv::why_this_matters.size() >= diag::insights_quality_thresholds<CrossTenantLeak>::min_why_chars);
static_assert(Pqv::symptom_pattern.size() >= diag::insights_quality_thresholds<CrossTenantLeak>::min_symptom_chars);

static_assert(diag::has_insights_v<PaymentRefundLeak>);

// A severity-only registration reports no insights, because all four prose
// fields are empty. That is the acknowledged-but-unwritten state, and it is
// deliberately distinguishable from a tag nobody has registered at all.
static_assert(!diag::has_insights_v<ScheduleDoubleAcceleration>);

static_assert(diag::has_insights_v<CrossTenantLeak>);
static_assert(diag::has_substantive_insights_v<CrossTenantLeak>);

static_assert(diag::WellInsightedTag<PaymentRefundLeak>);
static_assert(!diag::WellInsightedTag<ScheduleDoubleAcceleration>);
static_assert(diag::WellInsightedTag<CrossTenantLeak>);

// The substantive gate is the stronger of the two.
static_assert(!diag::HasSubstantiveInsights<PaymentRefundLeak> || diag::has_substantive_insights_v<PaymentRefundLeak>);
static_assert(diag::HasSubstantiveInsights<CrossTenantLeak>);

static_assert(!diag::WellInsightedTag<int>);
static_assert(!diag::WellInsightedTag<void>);

}  // namespace user_proj::diag_tags::self_test

// Reading the accessors at runtime forces the instantiations into the binary,
// which nothing above does.

int main() {
    namespace diag = ::crucible::safety::diag;

    auto sev_full = diag::insight_provider<::user_proj::diag_tags::PaymentRefundLeak>::severity;
    auto sev_qv = diag::insight_provider<::user_proj::diag_tags::CrossTenantLeak>::severity;

    if (diag::severity_name(sev_full).empty()) return 1;
    if (diag::severity_name(sev_qv).empty()) return 2;

    // The header's own smoke bodies run from static initializers before this
    // point, so arriving here means none of them aborted.
    return 0;
}
