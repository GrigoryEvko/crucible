#pragma once

// The substrate catalog and its category enum are closed. The fixy per-axis
// diagnostic tags therefore live in a separate closed enumeration, next to the
// tag classes that define them. This header stays on the substrate re-exports
// and does not pull the engagement-gate machinery in.

#include <crucible/safety/_Diagnostic.h>
#include <crucible/safety/diag/_Insights.h>
#include <crucible/safety/diag/_RowHashFold.h>
#include <crucible/safety/diag/_StableName.h>

namespace crucible::fixy::diag {

using ::crucible::safety::diag::tag_base;

using ::crucible::safety::diag::EffectRowMismatch;
using ::crucible::safety::diag::UnknownParameterShape;
using ::crucible::safety::diag::GradedWrapperViolation;
using ::crucible::safety::diag::LinearityViolation;
using ::crucible::safety::diag::RefinementViolation;
using ::crucible::safety::diag::HotPathViolation;
using ::crucible::safety::diag::DetSafeLeak;
using ::crucible::safety::diag::NumericalTierMismatch;
using ::crucible::safety::diag::MemOrderViolation;
using ::crucible::safety::diag::AllocClassViolation;
using ::crucible::safety::diag::VendorBackendMismatch;
using ::crucible::safety::diag::CrashClassMismatch;
using ::crucible::safety::diag::ConsistencyMismatch;
using ::crucible::safety::diag::LifetimeViolation;
using ::crucible::safety::diag::WaitStrategyViolation;
using ::crucible::safety::diag::ProgressClassViolation;
using ::crucible::safety::diag::CipherTierViolation;
using ::crucible::safety::diag::ResidencyHeatViolation;
using ::crucible::safety::diag::EpochMismatch;
using ::crucible::safety::diag::BudgetExceeded;
using ::crucible::safety::diag::NumaPlacementMismatch;
using ::crucible::safety::diag::RecipeSpecMismatch;
using ::crucible::safety::diag::PureFunctionViolation;
using ::crucible::safety::diag::DivergenceBudgetViolation;
using ::crucible::safety::diag::StateBudgetViolation;
using ::crucible::safety::diag::InsufficientWitness;
using ::crucible::safety::diag::ModalityMismatch;
using ::crucible::safety::diag::LinearAliasViolation;
using ::crucible::safety::diag::SharedPermissionPoolSaturated;
using ::crucible::safety::diag::HugePageAllocationFailed;
using ::crucible::safety::diag::PublishOnceDoublePublish;
using ::crucible::safety::diag::BitsInvariantViolation;
using ::crucible::safety::diag::BorrowedBoundsViolation;

using ::crucible::safety::diag::Catalog;
using ::crucible::safety::diag::catalog_size;

template <typename T>
inline constexpr bool is_diagnostic_class_v = ::crucible::safety::diag::is_diagnostic_class_v<T>;

template <typename T>
inline constexpr std::string_view diagnostic_name_v = ::crucible::safety::diag::diagnostic_name_v<T>;

template <typename T>
inline constexpr std::string_view diagnostic_description_v = ::crucible::safety::diag::diagnostic_description_v<T>;

template <typename T>
inline constexpr std::string_view diagnostic_remediation_v = ::crucible::safety::diag::diagnostic_remediation_v<T>;

template <typename DiagnosticClass, typename... Context>
using Diagnostic = ::crucible::safety::diag::Diagnostic<DiagnosticClass, Context...>;

template <typename T>
inline constexpr bool is_diagnostic_v = ::crucible::safety::diag::is_diagnostic_v<T>;

using ::crucible::safety::diag::Category;

template <Category C>
using tag_of_t = ::crucible::safety::diag::tag_of_t<C>;

template <typename Tag>
inline constexpr Category category_of_v = ::crucible::safety::diag::category_of_v<Tag>;

// The array holds every Category in catalog order.
using ::crucible::safety::diag::categories_v;

template <typename T>
inline constexpr std::string_view stable_name_of = ::crucible::safety::diag::stable_name_of<T>;

template <typename T>
inline constexpr std::uint64_t stable_type_id = ::crucible::safety::diag::stable_type_id<T>;

template <auto FnPtr>
inline constexpr std::uint64_t stable_function_id = ::crucible::safety::diag::stable_function_id<FnPtr>;

template <typename... Ts>
using canonicalize_pack_t = ::crucible::safety::diag::canonicalize_pack_t<Ts...>;

inline constexpr std::uint64_t FNV1A_OFFSET_BASIS = ::crucible::safety::diag::detail::FNV1A_OFFSET_BASIS;
inline constexpr std::uint64_t FNV1A_PRIME = ::crucible::safety::diag::detail::FNV1A_PRIME;

template <typename Tag>
using insight_provider = ::crucible::safety::diag::insight_provider<Tag>;

template <typename Tag>
using insights_quality_thresholds = ::crucible::safety::diag::insights_quality_thresholds<Tag>;

template <typename T>
using row_hash_contribution = ::crucible::safety::diag::row_hash_contribution<T>;

template <typename T>
inline constexpr std::uint64_t row_hash_contribution_v = ::crucible::safety::diag::row_hash_contribution_v<T>;

inline constexpr std::uint64_t EMPTY_ROW_HASH = ::crucible::safety::diag::detail::EMPTY_ROW_HASH;

// Every diagnostic construction goes through this factory so the
// authorization points stay on one grep target. Calling the Diagnostic
// constructor directly bypasses that surface.
using ::crucible::safety::diag::mint_diagnostic;

}  // namespace crucible::fixy::diag

namespace crucible::fixy::diag::self_test {

static_assert(Category::EffectRowMismatch == ::crucible::safety::diag::Category::EffectRowMismatch);
static_assert(Category::LinearAliasViolation == ::crucible::safety::diag::Category::LinearAliasViolation);

// The exact ceiling pin sits beside the substrate constant. This side holds
// only a floor, so an append-only catalog bump tracks here on its own and only
// a removal reddens.
static_assert(catalog_size >= 31, "fixy::diag::catalog_size floor: regressed below 31 — a Catalog "
                                  "entry was removed without updating both the substrate ceiling "
                                  "pin AND this floor witness.");

static_assert(std::is_same_v<HotPathViolation, ::crucible::safety::diag::HotPathViolation>,
              "fixy::diag::HotPathViolation must alias the substrate tag class");

static_assert(
    std::is_same_v<Diagnostic<HotPathViolation, int, float>,
                   ::crucible::safety::diag::Diagnostic<::crucible::safety::diag::HotPathViolation, int, float>>,
    "fixy::diag::Diagnostic must alias safety::diag::Diagnostic");

static_assert(category_of_v<HotPathViolation> == Category::HotPathViolation);
static_assert(std::is_same_v<tag_of_t<Category::HotPathViolation>, HotPathViolation>);

static_assert(is_diagnostic_class_v<HotPathViolation>);
static_assert(!is_diagnostic_class_v<int>);
static_assert(!is_diagnostic_class_v<tag_base>);

struct DiagSentinelStableName_TypeA {};
static_assert(!stable_name_of<DiagSentinelStableName_TypeA>.empty());

static_assert(EMPTY_ROW_HASH == ::crucible::safety::diag::detail::EMPTY_ROW_HASH);

static_assert(std::is_same_v<SharedPermissionPoolSaturated, ::crucible::safety::diag::SharedPermissionPoolSaturated>,
              "fixy::diag::SharedPermissionPoolSaturated must alias substrate tag");
static_assert(std::is_same_v<HugePageAllocationFailed, ::crucible::safety::diag::HugePageAllocationFailed>,
              "fixy::diag::HugePageAllocationFailed must alias substrate tag");
static_assert(std::is_same_v<PublishOnceDoublePublish, ::crucible::safety::diag::PublishOnceDoublePublish>,
              "fixy::diag::PublishOnceDoublePublish must alias substrate tag");
static_assert(std::is_same_v<BitsInvariantViolation, ::crucible::safety::diag::BitsInvariantViolation>,
              "fixy::diag::BitsInvariantViolation must alias substrate tag");
static_assert(std::is_same_v<BorrowedBoundsViolation, ::crucible::safety::diag::BorrowedBoundsViolation>,
              "fixy::diag::BorrowedBoundsViolation must alias substrate tag");

static_assert(category_of_v<SharedPermissionPoolSaturated> == Category::SharedPermissionPoolSaturated);
static_assert(category_of_v<HugePageAllocationFailed> == Category::HugePageAllocationFailed);
static_assert(category_of_v<PublishOnceDoublePublish> == Category::PublishOnceDoublePublish);
static_assert(category_of_v<BitsInvariantViolation> == Category::BitsInvariantViolation);
static_assert(category_of_v<BorrowedBoundsViolation> == Category::BorrowedBoundsViolation);
static_assert(std::is_same_v<tag_of_t<Category::SharedPermissionPoolSaturated>, SharedPermissionPoolSaturated>);
static_assert(std::is_same_v<tag_of_t<Category::HugePageAllocationFailed>, HugePageAllocationFailed>);
static_assert(std::is_same_v<tag_of_t<Category::PublishOnceDoublePublish>, PublishOnceDoublePublish>);
static_assert(std::is_same_v<tag_of_t<Category::BitsInvariantViolation>, BitsInvariantViolation>);
static_assert(std::is_same_v<tag_of_t<Category::BorrowedBoundsViolation>, BorrowedBoundsViolation>);

static_assert(is_diagnostic_class_v<SharedPermissionPoolSaturated>);
static_assert(is_diagnostic_class_v<HugePageAllocationFailed>);
static_assert(is_diagnostic_class_v<PublishOnceDoublePublish>);
static_assert(is_diagnostic_class_v<BitsInvariantViolation>);
static_assert(is_diagnostic_class_v<BorrowedBoundsViolation>);

static_assert(!insight_provider<SharedPermissionPoolSaturated>::why_this_matters.empty(),
              "SharedPermissionPoolSaturated insight_provider must be specialized");
static_assert(!insight_provider<HugePageAllocationFailed>::why_this_matters.empty(),
              "HugePageAllocationFailed insight_provider must be specialized");
static_assert(!insight_provider<PublishOnceDoublePublish>::why_this_matters.empty(),
              "PublishOnceDoublePublish insight_provider must be specialized");
static_assert(!insight_provider<BitsInvariantViolation>::why_this_matters.empty(),
              "BitsInvariantViolation insight_provider must be specialized");
static_assert(!insight_provider<BorrowedBoundsViolation>::why_this_matters.empty(),
              "BorrowedBoundsViolation insight_provider must be specialized");

// The witness compares deduced return types rather than function pointers.
// mint_diagnostic is an immediate function, and a pointer to one cannot be
// formed outside an immediate-function context. A static_assert is such a
// context, so the call itself is well formed here.
static_assert(
    std::is_same_v<decltype(::crucible::fixy::diag::mint_diagnostic<::crucible::fixy::diag::HotPathViolation>()),
                   decltype(::crucible::safety::diag::mint_diagnostic<::crucible::safety::diag::HotPathViolation>())>,
    "fixy::diag::mint_diagnostic must alias safety::diag::mint_diagnostic.");

}  // namespace crucible::fixy::diag::self_test
