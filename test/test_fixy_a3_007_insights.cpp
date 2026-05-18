// fixy-A3-007 sentinel TU: six Diagnostic.h tags previously missing
// insight_provider specializations now ship substantive insights.
//
// `has_insights_v<Tag>`             — any prose field non-empty.
// `has_substantive_insights_v<Tag>` — every prose field meets QV
//                                      minimums (30/20/10/10 chars).
//
// Companion to test/test_fixy_insights.cpp (fixy-H-14): that TU covers
// the 20 FixyNotEngaged_<Axis> tags and the 4 §30.14 corpus entries
// in fixy/Insights.h.  This TU covers the 6 substrate-level tags that
// fixy-H-14 did NOT — PureFunctionViolation, DivergenceBudgetViolation,
// StateBudgetViolation, InsufficientWitness, ModalityMismatch,
// LinearAliasViolation — which live in safety/Diagnostic.h and are
// consumed by FxAliases.h (Pure/Div/ST) and fixy/Grant.h (witness +
// modality discipline).

#include <crucible/safety/Diagnostic.h>
#include <crucible/safety/diag/Insights.h>

namespace {

using ::crucible::safety::diag::has_insights_v;
using ::crucible::safety::diag::has_substantive_insights_v;
using ::crucible::safety::diag::insight_provider;
using ::crucible::safety::diag::Severity;

#define CRUCIBLE_CHECK_A3_007_INSIGHT(TagName)                                  \
    static_assert(has_insights_v<::crucible::safety::diag::TagName>,            \
        "fixy-A3-007: " #TagName                                                \
        " must have an insight_provider specialization");                       \
    static_assert(has_substantive_insights_v<                                   \
        ::crucible::safety::diag::TagName>,                                     \
        "fixy-A3-007: " #TagName                                                \
        " insights must meet QV thresholds (30/20/10/10 chars)")

CRUCIBLE_CHECK_A3_007_INSIGHT(PureFunctionViolation);
CRUCIBLE_CHECK_A3_007_INSIGHT(DivergenceBudgetViolation);
CRUCIBLE_CHECK_A3_007_INSIGHT(StateBudgetViolation);
CRUCIBLE_CHECK_A3_007_INSIGHT(InsufficientWitness);
CRUCIBLE_CHECK_A3_007_INSIGHT(ModalityMismatch);
CRUCIBLE_CHECK_A3_007_INSIGHT(LinearAliasViolation);

#undef CRUCIBLE_CHECK_A3_007_INSIGHT

// ── Severity sanity — all six ship Severity::Error (foundation default).
// Promoting any of them to Fatal requires re-auditing the consumers that
// would lose their no-override-path admission; that promotion is OUT of
// scope for fixy-A3-007 (insight-coverage closure only).

static_assert(
    insight_provider<::crucible::safety::diag::PureFunctionViolation>
        ::severity == Severity::Error,
    "PureFunctionViolation severity floor is Error — promotion to Fatal "
    "requires re-auditing FxAliases Pure<T> / Tot<E, T> consumers.");

static_assert(
    insight_provider<::crucible::safety::diag::InsufficientWitness>
        ::severity == Severity::Error,
    "InsufficientWitness severity floor is Error — promotion to Fatal "
    "requires re-auditing the witness-floor consumers in Cipher / "
    "Federation / AdaptiveScheduler.");

}  // namespace

int main() { return 0; }
