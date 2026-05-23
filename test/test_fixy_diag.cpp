// ── test_fixy_diag — sentinel TU for fixy/Diag.h ───────────────────
//
// Pulls fixy/Diag.h into a TU compiled under project warning flags so
// the header's static_asserts execute under enforcement.  Witnesses:
//
//   1. Category enum + Catalog tuple identity.
//   2. tag_of_t ↔ category_of_v bidirectional map preserved through
//      the alias.
//   3. is_diagnostic_class_v rejects non-tags, accepts every shipped
//      tag class.
//   4. Diagnostic<DiagnosticClass, Ctx...> wrapper identity.
//   5. stable_name_of / stable_type_id / stable_function_id preserved.
//   6. canonicalize_pack_t preserved.
//   7. row_hash_contribution_v / EMPTY_ROW_HASH preserved.
//   8. insight_provider primary template instantiable via alias.
//
// HS14: 2 fixy_neg fixtures live in test/fixy_neg/
//       neg_fixy_diag_*.cpp.

#include <crucible/fixy/Diag.h>

#include <type_traits>

namespace fd = crucible::fixy::diag;
namespace cd = crucible::safety::diag;

// ─── 1. Category enum + Catalog identity ──────────────────────────

static_assert(fd::Category::EffectRowMismatch      == cd::Category::EffectRowMismatch);
static_assert(fd::Category::HotPathViolation       == cd::Category::HotPathViolation);
static_assert(fd::Category::DetSafeLeak            == cd::Category::DetSafeLeak);
static_assert(fd::Category::LinearityViolation     == cd::Category::LinearityViolation);
static_assert(fd::Category::LinearAliasViolation   == cd::Category::LinearAliasViolation);

// FIXY-U-064: full 31-tag Category coverage table.  Every Category
// value MUST round-trip — tag_of_t<C>'s back-pointer through
// category_of_v MUST equal C.  This catches any future enum / Catalog
// / specialization drift in a single static_assert per entry.
//
// FIXY-U-128 / U-129 floor-vs-ceiling split: the EXACT ceiling pin
// (`== 31`) lives in fixy/Diag.h colocated with the source-of-truth
// constant; THIS TU only holds the FLOOR pin (`>= 31`) which catches
// the inverse direction — an accidental REMOVAL of a Category tag.
static_assert(fd::catalog_size == cd::catalog_size);
static_assert(fd::catalog_size >= 31,
    "floor: fixy::diag::catalog_size regressed below 31 — a Category "
    "tag was removed without updating both Diag.h's colocated ceiling "
    "pin AND this floor witness.");

static_assert(std::is_same_v<fd::Catalog, cd::Catalog>);

// All 31 Category values reachable through fd::Category.
static_assert(fd::Category::EffectRowMismatch              == cd::Category::EffectRowMismatch);
static_assert(fd::Category::UnknownParameterShape         == cd::Category::UnknownParameterShape);
static_assert(fd::Category::GradedWrapperViolation        == cd::Category::GradedWrapperViolation);
static_assert(fd::Category::LinearityViolation            == cd::Category::LinearityViolation);
static_assert(fd::Category::RefinementViolation           == cd::Category::RefinementViolation);
static_assert(fd::Category::HotPathViolation              == cd::Category::HotPathViolation);
static_assert(fd::Category::DetSafeLeak                   == cd::Category::DetSafeLeak);
static_assert(fd::Category::NumericalTierMismatch         == cd::Category::NumericalTierMismatch);
static_assert(fd::Category::MemOrderViolation             == cd::Category::MemOrderViolation);
static_assert(fd::Category::AllocClassViolation           == cd::Category::AllocClassViolation);
static_assert(fd::Category::VendorBackendMismatch         == cd::Category::VendorBackendMismatch);
static_assert(fd::Category::CrashClassMismatch            == cd::Category::CrashClassMismatch);
static_assert(fd::Category::ConsistencyMismatch           == cd::Category::ConsistencyMismatch);
static_assert(fd::Category::LifetimeViolation             == cd::Category::LifetimeViolation);
static_assert(fd::Category::WaitStrategyViolation         == cd::Category::WaitStrategyViolation);
static_assert(fd::Category::ProgressClassViolation        == cd::Category::ProgressClassViolation);
static_assert(fd::Category::CipherTierViolation           == cd::Category::CipherTierViolation);
static_assert(fd::Category::ResidencyHeatViolation        == cd::Category::ResidencyHeatViolation);
static_assert(fd::Category::EpochMismatch                 == cd::Category::EpochMismatch);
static_assert(fd::Category::BudgetExceeded                == cd::Category::BudgetExceeded);
static_assert(fd::Category::NumaPlacementMismatch         == cd::Category::NumaPlacementMismatch);
static_assert(fd::Category::RecipeSpecMismatch            == cd::Category::RecipeSpecMismatch);
static_assert(fd::Category::PureFunctionViolation         == cd::Category::PureFunctionViolation);
static_assert(fd::Category::DivergenceBudgetViolation     == cd::Category::DivergenceBudgetViolation);
static_assert(fd::Category::StateBudgetViolation          == cd::Category::StateBudgetViolation);
static_assert(fd::Category::InsufficientWitness           == cd::Category::InsufficientWitness);
static_assert(fd::Category::ModalityMismatch              == cd::Category::ModalityMismatch);
static_assert(fd::Category::LinearAliasViolation          == cd::Category::LinearAliasViolation);
static_assert(fd::Category::SharedPermissionPoolSaturated == cd::Category::SharedPermissionPoolSaturated);
static_assert(fd::Category::HugePageAllocationFailed      == cd::Category::HugePageAllocationFailed);
static_assert(fd::Category::PublishOnceDoublePublish      == cd::Category::PublishOnceDoublePublish);
static_assert(fd::Category::BitsInvariantViolation        == cd::Category::BitsInvariantViolation);
static_assert(fd::Category::BorrowedBoundsViolation       == cd::Category::BorrowedBoundsViolation);

// ─── 2. Bidirectional map round-trip ──────────────────────────────
//
// Exhaustive — every Category value MUST resolve through tag_of_t back
// to a tag whose category_of_v matches.  33 round-trips.

#define DIAG_ROUNDTRIP(name) \
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

// ─── 3. is_diagnostic_class_v ─────────────────────────────────────

struct DiagSentinel_NotATag {};
static_assert(!fd::is_diagnostic_class_v<DiagSentinel_NotATag>);
static_assert(!fd::is_diagnostic_class_v<int>);
static_assert(!fd::is_diagnostic_class_v<fd::tag_base>);  // base itself excluded

// Sample of shipped tags.
static_assert(fd::is_diagnostic_class_v<fd::HotPathViolation>);
static_assert(fd::is_diagnostic_class_v<fd::DetSafeLeak>);
static_assert(fd::is_diagnostic_class_v<fd::LinearityViolation>);
static_assert(fd::is_diagnostic_class_v<fd::RefinementViolation>);
static_assert(fd::is_diagnostic_class_v<fd::GradedWrapperViolation>);
static_assert(fd::is_diagnostic_class_v<fd::NumericalTierMismatch>);
static_assert(fd::is_diagnostic_class_v<fd::EffectRowMismatch>);
static_assert(fd::is_diagnostic_class_v<fd::CipherTierViolation>);
static_assert(fd::is_diagnostic_class_v<fd::ResidencyHeatViolation>);
// FIXY-U-064: 3 new tags from substrate (entries 28-30).
static_assert(fd::is_diagnostic_class_v<fd::SharedPermissionPoolSaturated>);
static_assert(fd::is_diagnostic_class_v<fd::HugePageAllocationFailed>);
static_assert(fd::is_diagnostic_class_v<fd::PublishOnceDoublePublish>);
// WRAP-Bits-Borrowed-Diagnostic #1092: 2 new tags (entries 31-32).
static_assert(fd::is_diagnostic_class_v<fd::BitsInvariantViolation>);
static_assert(fd::is_diagnostic_class_v<fd::BorrowedBoundsViolation>);

// Accessors return non-empty strings.
static_assert(!fd::diagnostic_name_v<fd::HotPathViolation>.empty());
static_assert(!fd::diagnostic_description_v<fd::HotPathViolation>.empty());
static_assert(!fd::diagnostic_remediation_v<fd::HotPathViolation>.empty());

// ─── 4. Diagnostic<DiagnosticClass, Ctx...> ───────────────────────

struct DiagSentinel_CtxA {};
struct DiagSentinel_CtxB {};

using D = fd::Diagnostic<fd::HotPathViolation, DiagSentinel_CtxA, DiagSentinel_CtxB>;
static_assert(std::is_same_v<D,
    cd::Diagnostic<cd::HotPathViolation, DiagSentinel_CtxA, DiagSentinel_CtxB>>);

static_assert(fd::is_diagnostic_v<D>);
static_assert(!fd::is_diagnostic_v<int>);

// ─── 5. StableName fingerprints ───────────────────────────────────

struct DiagSentinel_FingerprintA {};
struct DiagSentinel_FingerprintB {};

static_assert(!fd::stable_name_of<DiagSentinel_FingerprintA>.empty());
static_assert(fd::stable_type_id<DiagSentinel_FingerprintA> != 0);
static_assert(fd::stable_type_id<DiagSentinel_FingerprintA>
              != fd::stable_type_id<DiagSentinel_FingerprintB>,
    "stable_type_id must distinguish distinct types");

// Substrate parity.
static_assert(fd::stable_type_id<DiagSentinel_FingerprintA>
              == cd::stable_type_id<DiagSentinel_FingerprintA>);

// ─── 6. canonicalize_pack_t ───────────────────────────────────────

static_assert(std::is_same_v<
    fd::canonicalize_pack_t<int, float, double>,
    cd::canonicalize_pack_t<int, float, double>>);

// ─── 7. row_hash_contribution / EMPTY_ROW_HASH ────────────────────

static_assert(fd::EMPTY_ROW_HASH == cd::detail::EMPTY_ROW_HASH);

static_assert(fd::row_hash_contribution_v<crucible::effects::Row<>>
              == cd::row_hash_contribution_v<crucible::effects::Row<>>);

// ─── 8. insight_provider instantiable ─────────────────────────────

// Primary template (empty defaults) is well-formed for any T.
using DiagSentinel_DefaultInsight = fd::insight_provider<DiagSentinel_CtxA>;
static_assert(sizeof(DiagSentinel_DefaultInsight) >= 1);

// Specialized for a shipped tag.
using DiagSentinel_HotpathInsight = fd::insight_provider<fd::HotPathViolation>;
static_assert(sizeof(DiagSentinel_HotpathInsight) >= 1);

// FIXY-U-064: 3 new tag insight_providers must be substantively
// populated (NOT the empty primary template).  Empty defaults would
// indicate the substrate-side insight_provider sweep regressed.
static_assert(!fd::insight_provider<fd::SharedPermissionPoolSaturated>::why_this_matters.empty(),
    "SharedPermissionPoolSaturated insight_provider must be specialized");
static_assert(!fd::insight_provider<fd::HugePageAllocationFailed>::why_this_matters.empty(),
    "HugePageAllocationFailed insight_provider must be specialized");
static_assert(!fd::insight_provider<fd::PublishOnceDoublePublish>::why_this_matters.empty(),
    "PublishOnceDoublePublish insight_provider must be specialized");
// All 4 prose fields populated — the "sweep" calls for substantive
// content per CRUCIBLE_DEFINE_INSIGHTS macro discipline.
static_assert(!fd::insight_provider<fd::SharedPermissionPoolSaturated>::symptom_pattern.empty());
static_assert(!fd::insight_provider<fd::SharedPermissionPoolSaturated>::correct_example.empty());
static_assert(!fd::insight_provider<fd::SharedPermissionPoolSaturated>::violating_example.empty());
static_assert(!fd::insight_provider<fd::HugePageAllocationFailed>::symptom_pattern.empty());
static_assert(!fd::insight_provider<fd::HugePageAllocationFailed>::correct_example.empty());
static_assert(!fd::insight_provider<fd::HugePageAllocationFailed>::violating_example.empty());
static_assert(!fd::insight_provider<fd::PublishOnceDoublePublish>::symptom_pattern.empty());
static_assert(!fd::insight_provider<fd::PublishOnceDoublePublish>::correct_example.empty());
static_assert(!fd::insight_provider<fd::PublishOnceDoublePublish>::violating_example.empty());
// WRAP-Bits-Borrowed-Diagnostic #1092: Bits + Borrowed runtime violation tags.
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

// ─── 9. Runtime sanity — emit a Category through the alias ────────

int main() {
    fd::Category cat = fd::category_of_v<fd::HotPathViolation>;
    switch (cat) {
        case fd::Category::HotPathViolation: return 0;
        default:                              return 1;
    }
}
