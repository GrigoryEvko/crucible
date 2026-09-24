// Everything checked here is reached through the re-exporting namespace
// and compared against the substrate it re-exports.  A re-export that
// quietly became a copy would satisfy every use site and fail these
// identities.

#include <crucible/fixy/Diag.h>

#include <type_traits>

namespace fd = crucible::fixy::diag;
namespace cd = crucible::safety::diag;

static_assert(fd::Category::EffectRowMismatch == cd::Category::EffectRowMismatch);
static_assert(fd::Category::HotPathViolation == cd::Category::HotPathViolation);
static_assert(fd::Category::DetSafeLeak == cd::Category::DetSafeLeak);
static_assert(fd::Category::LinearityViolation == cd::Category::LinearityViolation);
static_assert(fd::Category::LinearAliasViolation == cd::Category::LinearAliasViolation);

// The bound here is a floor.  The exact size is pinned next to the
// constant it describes, and this one catches the opposite mistake: a
// tag removed rather than appended.
static_assert(fd::catalog_size == cd::catalog_size);
static_assert(fd::catalog_size >= 31, "the diagnostic catalog has lost a tag: it may only grow.");

static_assert(std::is_same_v<fd::Catalog, cd::Catalog>);

// Every category is listed rather than sampled, so a value that exists
// on one side and not the other cannot pass.
static_assert(fd::Category::EffectRowMismatch == cd::Category::EffectRowMismatch);
static_assert(fd::Category::UnknownParameterShape == cd::Category::UnknownParameterShape);
static_assert(fd::Category::GradedWrapperViolation == cd::Category::GradedWrapperViolation);
static_assert(fd::Category::LinearityViolation == cd::Category::LinearityViolation);
static_assert(fd::Category::RefinementViolation == cd::Category::RefinementViolation);
static_assert(fd::Category::HotPathViolation == cd::Category::HotPathViolation);
static_assert(fd::Category::DetSafeLeak == cd::Category::DetSafeLeak);
static_assert(fd::Category::NumericalTierMismatch == cd::Category::NumericalTierMismatch);
static_assert(fd::Category::MemOrderViolation == cd::Category::MemOrderViolation);
static_assert(fd::Category::AllocClassViolation == cd::Category::AllocClassViolation);
static_assert(fd::Category::VendorBackendMismatch == cd::Category::VendorBackendMismatch);
static_assert(fd::Category::CrashClassMismatch == cd::Category::CrashClassMismatch);
static_assert(fd::Category::ConsistencyMismatch == cd::Category::ConsistencyMismatch);
static_assert(fd::Category::LifetimeViolation == cd::Category::LifetimeViolation);
static_assert(fd::Category::WaitStrategyViolation == cd::Category::WaitStrategyViolation);
static_assert(fd::Category::ProgressClassViolation == cd::Category::ProgressClassViolation);
static_assert(fd::Category::CipherTierViolation == cd::Category::CipherTierViolation);
static_assert(fd::Category::ResidencyHeatViolation == cd::Category::ResidencyHeatViolation);
static_assert(fd::Category::EpochMismatch == cd::Category::EpochMismatch);
static_assert(fd::Category::BudgetExceeded == cd::Category::BudgetExceeded);
static_assert(fd::Category::NumaPlacementMismatch == cd::Category::NumaPlacementMismatch);
static_assert(fd::Category::RecipeSpecMismatch == cd::Category::RecipeSpecMismatch);
static_assert(fd::Category::PureFunctionViolation == cd::Category::PureFunctionViolation);
static_assert(fd::Category::DivergenceBudgetViolation == cd::Category::DivergenceBudgetViolation);
static_assert(fd::Category::StateBudgetViolation == cd::Category::StateBudgetViolation);
static_assert(fd::Category::InsufficientWitness == cd::Category::InsufficientWitness);
static_assert(fd::Category::ModalityMismatch == cd::Category::ModalityMismatch);
static_assert(fd::Category::LinearAliasViolation == cd::Category::LinearAliasViolation);
static_assert(fd::Category::SharedPermissionPoolSaturated == cd::Category::SharedPermissionPoolSaturated);
static_assert(fd::Category::HugePageAllocationFailed == cd::Category::HugePageAllocationFailed);
static_assert(fd::Category::PublishOnceDoublePublish == cd::Category::PublishOnceDoublePublish);
static_assert(fd::Category::BitsInvariantViolation == cd::Category::BitsInvariantViolation);
static_assert(fd::Category::BorrowedBoundsViolation == cd::Category::BorrowedBoundsViolation);

// Each entry walks both directions: the tag names a category, and that
// category names the tag back.  A half-written specialization passes
// one direction and fails the other.

#define DIAG_ROUNDTRIP(name)                                          \
    static_assert(fd::category_of_v<fd::name> == fd::Category::name); \
    static_assert(std::is_same_v<fd::tag_of_t<fd::Category::name>, fd::name>)

DIAG_ROUNDTRIP(EffectRowMismatch);
DIAG_ROUNDTRIP(UnknownParameterShape);
DIAG_ROUNDTRIP(GradedWrapperViolation);
DIAG_ROUNDTRIP(LinearityViolation);
DIAG_ROUNDTRIP(RefinementViolation);
DIAG_ROUNDTRIP(HotPathViolation);
DIAG_ROUNDTRIP(DetSafeLeak);
DIAG_ROUNDTRIP(NumericalTierMismatch);
DIAG_ROUNDTRIP(MemOrderViolation);
DIAG_ROUNDTRIP(AllocClassViolation);
DIAG_ROUNDTRIP(VendorBackendMismatch);
DIAG_ROUNDTRIP(CrashClassMismatch);
DIAG_ROUNDTRIP(ConsistencyMismatch);
DIAG_ROUNDTRIP(LifetimeViolation);
DIAG_ROUNDTRIP(WaitStrategyViolation);
DIAG_ROUNDTRIP(ProgressClassViolation);
DIAG_ROUNDTRIP(CipherTierViolation);
DIAG_ROUNDTRIP(ResidencyHeatViolation);
DIAG_ROUNDTRIP(EpochMismatch);
DIAG_ROUNDTRIP(BudgetExceeded);
DIAG_ROUNDTRIP(NumaPlacementMismatch);
DIAG_ROUNDTRIP(RecipeSpecMismatch);
DIAG_ROUNDTRIP(PureFunctionViolation);
DIAG_ROUNDTRIP(DivergenceBudgetViolation);
DIAG_ROUNDTRIP(StateBudgetViolation);
DIAG_ROUNDTRIP(InsufficientWitness);
DIAG_ROUNDTRIP(ModalityMismatch);
DIAG_ROUNDTRIP(LinearAliasViolation);
DIAG_ROUNDTRIP(SharedPermissionPoolSaturated);
DIAG_ROUNDTRIP(HugePageAllocationFailed);
DIAG_ROUNDTRIP(PublishOnceDoublePublish);
DIAG_ROUNDTRIP(BitsInvariantViolation);
DIAG_ROUNDTRIP(BorrowedBoundsViolation);

#undef DIAG_ROUNDTRIP

struct DiagSentinel_NotATag {};
static_assert(!fd::is_diagnostic_class_v<DiagSentinel_NotATag>);
static_assert(!fd::is_diagnostic_class_v<int>);
static_assert(!fd::is_diagnostic_class_v<fd::tag_base>);  // base itself excluded

static_assert(fd::is_diagnostic_class_v<fd::HotPathViolation>);
static_assert(fd::is_diagnostic_class_v<fd::DetSafeLeak>);
static_assert(fd::is_diagnostic_class_v<fd::LinearityViolation>);
static_assert(fd::is_diagnostic_class_v<fd::RefinementViolation>);
static_assert(fd::is_diagnostic_class_v<fd::GradedWrapperViolation>);
static_assert(fd::is_diagnostic_class_v<fd::NumericalTierMismatch>);
static_assert(fd::is_diagnostic_class_v<fd::EffectRowMismatch>);
static_assert(fd::is_diagnostic_class_v<fd::CipherTierViolation>);
static_assert(fd::is_diagnostic_class_v<fd::ResidencyHeatViolation>);
static_assert(fd::is_diagnostic_class_v<fd::SharedPermissionPoolSaturated>);
static_assert(fd::is_diagnostic_class_v<fd::HugePageAllocationFailed>);
static_assert(fd::is_diagnostic_class_v<fd::PublishOnceDoublePublish>);
static_assert(fd::is_diagnostic_class_v<fd::BitsInvariantViolation>);
static_assert(fd::is_diagnostic_class_v<fd::BorrowedBoundsViolation>);

static_assert(!fd::diagnostic_name_v<fd::HotPathViolation>.empty());
static_assert(!fd::diagnostic_description_v<fd::HotPathViolation>.empty());
static_assert(!fd::diagnostic_remediation_v<fd::HotPathViolation>.empty());

struct DiagSentinel_CtxA {};
struct DiagSentinel_CtxB {};

using D = fd::Diagnostic<fd::HotPathViolation, DiagSentinel_CtxA, DiagSentinel_CtxB>;
static_assert(std::is_same_v<D, cd::Diagnostic<cd::HotPathViolation, DiagSentinel_CtxA, DiagSentinel_CtxB>>);

static_assert(fd::is_diagnostic_v<D>);
static_assert(!fd::is_diagnostic_v<int>);

struct DiagSentinel_FingerprintA {};
struct DiagSentinel_FingerprintB {};

static_assert(!fd::stable_name_of<DiagSentinel_FingerprintA>.empty());
static_assert(fd::stable_type_id<DiagSentinel_FingerprintA> != 0);
static_assert(fd::stable_type_id<DiagSentinel_FingerprintA> != fd::stable_type_id<DiagSentinel_FingerprintB>,
              "stable_type_id must distinguish distinct types");

static_assert(fd::stable_type_id<DiagSentinel_FingerprintA> == cd::stable_type_id<DiagSentinel_FingerprintA>);

static_assert(std::is_same_v<fd::canonicalize_pack_t<int, float, double>, cd::canonicalize_pack_t<int, float, double>>);

static_assert(fd::EMPTY_ROW_HASH == cd::detail::EMPTY_ROW_HASH);

static_assert(fd::row_hash_contribution_v<crucible::effects::Row<>>
              == cd::row_hash_contribution_v<crucible::effects::Row<>>);

// The primary template accepts any type at all, which is why it can be
// instantiated on a type that is not a tag.
using DiagSentinel_DefaultInsight = fd::insight_provider<DiagSentinel_CtxA>;
static_assert(sizeof(DiagSentinel_DefaultInsight) >= 1);

using DiagSentinel_HotpathInsight = fd::insight_provider<fd::HotPathViolation>;
static_assert(sizeof(DiagSentinel_HotpathInsight) >= 1);

// That same permissiveness is the hazard: a tag with no specialization
// picks up the primary template and compiles with empty prose.  The
// emptiness checks below are what distinguish written text from the
// silent default.
static_assert(!fd::insight_provider<fd::SharedPermissionPoolSaturated>::why_this_matters.empty(),
              "SharedPermissionPoolSaturated insight_provider must be specialized");
static_assert(!fd::insight_provider<fd::HugePageAllocationFailed>::why_this_matters.empty(),
              "HugePageAllocationFailed insight_provider must be specialized");
static_assert(!fd::insight_provider<fd::PublishOnceDoublePublish>::why_this_matters.empty(),
              "PublishOnceDoublePublish insight_provider must be specialized");
// All four prose fields, not just the first: a specialization that
// filled one and left the rest empty would otherwise pass.
static_assert(!fd::insight_provider<fd::SharedPermissionPoolSaturated>::symptom_pattern.empty());
static_assert(!fd::insight_provider<fd::SharedPermissionPoolSaturated>::correct_example.empty());
static_assert(!fd::insight_provider<fd::SharedPermissionPoolSaturated>::violating_example.empty());
static_assert(!fd::insight_provider<fd::HugePageAllocationFailed>::symptom_pattern.empty());
static_assert(!fd::insight_provider<fd::HugePageAllocationFailed>::correct_example.empty());
static_assert(!fd::insight_provider<fd::HugePageAllocationFailed>::violating_example.empty());
static_assert(!fd::insight_provider<fd::PublishOnceDoublePublish>::symptom_pattern.empty());
static_assert(!fd::insight_provider<fd::PublishOnceDoublePublish>::correct_example.empty());
static_assert(!fd::insight_provider<fd::PublishOnceDoublePublish>::violating_example.empty());
static_assert(!fd::insight_provider<fd::BitsInvariantViolation>::why_this_matters.empty(),
              "BitsInvariantViolation insight_provider must be specialized");
static_assert(!fd::insight_provider<fd::BitsInvariantViolation>::symptom_pattern.empty());
static_assert(!fd::insight_provider<fd::BitsInvariantViolation>::correct_example.empty());
static_assert(!fd::insight_provider<fd::BitsInvariantViolation>::violating_example.empty());
static_assert(!fd::insight_provider<fd::BorrowedBoundsViolation>::why_this_matters.empty(),
              "BorrowedBoundsViolation insight_provider must be specialized");
static_assert(!fd::insight_provider<fd::BorrowedBoundsViolation>::symptom_pattern.empty());
static_assert(!fd::insight_provider<fd::BorrowedBoundsViolation>::correct_example.empty());
static_assert(!fd::insight_provider<fd::BorrowedBoundsViolation>::violating_example.empty());

// The switch has no shared arm with the default, so a category that
// resolved to the wrong tag would exit non-zero rather than compile
// away.
int main() {
    fd::Category cat = fd::category_of_v<fd::HotPathViolation>;
    switch (cat) {
        case fd::Category::HotPathViolation:
            return 0;
        default:
            return 1;
    }
}
