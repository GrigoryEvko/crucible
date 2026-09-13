#include <crucible/safety/Diagnostic.h>
#include <crucible/safety/diag/Insights.h>

namespace {

using ::crucible::safety::diag::has_insights_v;
using ::crucible::safety::diag::has_substantive_insights_v;
using ::crucible::safety::diag::insight_provider;
using ::crucible::safety::diag::Severity;

#define CRUCIBLE_CHECK_SUBSTANTIVE_INSIGHT(TagName)                              \
    static_assert(has_insights_v<::crucible::safety::diag::TagName>,             \
                  #TagName " must have an insight_provider specialization");     \
    static_assert(has_substantive_insights_v<::crucible::safety::diag::TagName>, \
                  #TagName " insights must meet QV thresholds (30/20/10/10 chars)")

CRUCIBLE_CHECK_SUBSTANTIVE_INSIGHT(PureFunctionViolation);
CRUCIBLE_CHECK_SUBSTANTIVE_INSIGHT(DivergenceBudgetViolation);
CRUCIBLE_CHECK_SUBSTANTIVE_INSIGHT(StateBudgetViolation);
CRUCIBLE_CHECK_SUBSTANTIVE_INSIGHT(InsufficientWitness);
CRUCIBLE_CHECK_SUBSTANTIVE_INSIGHT(ModalityMismatch);
CRUCIBLE_CHECK_SUBSTANTIVE_INSIGHT(LinearAliasViolation);

#undef CRUCIBLE_CHECK_SUBSTANTIVE_INSIGHT

static_assert(insight_provider<::crucible::safety::diag::PureFunctionViolation>::severity == Severity::Error,
              "PureFunctionViolation severity floor is Error — promotion to Fatal "
              "requires re-auditing FxAliases Pure<T> / Tot<E, T> consumers.");

static_assert(insight_provider<::crucible::safety::diag::InsufficientWitness>::severity == Severity::Error,
              "InsufficientWitness severity floor is Error — promotion to Fatal "
              "requires re-auditing the witness-floor consumers in Cipher / "
              "Federation / AdaptiveScheduler.");

}  // namespace

int main() { return 0; }
