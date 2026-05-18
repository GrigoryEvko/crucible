#pragma once

// ── crucible::fixy::diag — Diagnostic infrastructure ───────────────
//
// Phase D re-export per misc/16_05_2026_fixy.md.  Surfaces the
// classified-diagnostic substrate — `Category` enum, `tag_base`,
// the 30-entry `Catalog` tuple, bidirectional `tag_of_t` /
// `category_of_v` map, `Diagnostic<DiagnosticClass, Ctx...>`
// wrapper, stable-name introspection (`stable_name_of`,
// `stable_type_id`, `stable_function_id`, `canonicalize_pack`),
// per-tag `insight_provider`, and the `row_hash_contribution<W>`
// fold over the canonical wrapper-nesting order — under
// `fixy::diag::` so callers who include only the fixy umbrella
// never have to descend into the safety/diag/ tree.
//
// Per CLAUDE.md §XXI Universal Mint Pattern: there are no mints in
// this header — these are pure type-system witnesses and consteval
// helpers.  Aliases preserve substrate template identity exactly.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   safety/Diagnostic.h        — Category, tag_base, Catalog,
//                                tag_of_t, category_of_v, Diagnostic,
//                                28 diagnostic tag classes
//   safety/diag/StableName.h   — stable_name_of / stable_type_id /
//                                canonicalize_pack / stable_function_id
//   safety/diag/Insights.h     — insight_provider / quality thresholds
//   safety/diag/RowHashFold.h  — row_hash_contribution / EMPTY_ROW_HASH
//
// ── FixyCatalog reconciliation surface ─────────────────────────────
//
// Substrate `Catalog` (28 entries) and `Category` enum are CLOSED to
// the foundation per FOUND-E01.  Fixy's twenty `FixyNotEngaged_*`
// per-axis diagnostic tags live in a parallel closed enumeration
// `fixy::diag::FixyCatalog` defined in `fixy/Reject.h` (because the
// tag classes themselves are defined there).
//
// Callers who want the fixy-side catalog enumeration + `is_fixy_diag_v`
// discriminator + `axis_for_tag_v` reverse lookup include
// `crucible/fixy/Reject.h` (or the Fixy.h umbrella).  This header
// stays focused on the substrate re-exports and does not pull the
// engagement gate machinery in.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — tag classes are stateless structs; stable_type_id is
//              consteval — every value is bit-exact at compile time.
//   TypeSafe — Catalog tuple uses tag-class types directly; alias
//              passes through.  Category enum has uint8_t underlying
//              type; bijection enforced by self-test.
//   NullSafe — diagnostic surface carries no pointers; string_views
//              point into constexpr literals only.
//   MemSafe  — every diagnostic value is a constexpr / consteval
//              entity; no heap, no lifetime concerns.
//   DetSafe  — stable_type_id<T> is deterministic across build
//              (FNV-1a + fmix64); same T always produces same id.
//              row_hash_contribution federation cache keys depend on
//              this bit-stability; alias is zero-symbol.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  using-declarations + namespace-alias; no new types, no new
// constexpr values.  Same compile-time hash output via either path.

#include <crucible/safety/Diagnostic.h>
#include <crucible/safety/diag/Insights.h>
#include <crucible/safety/diag/RowHashFold.h>
#include <crucible/safety/diag/StableName.h>

namespace crucible::fixy::diag {

// ═══════════════════════════════════════════════════════════════════
// Diagnostic catalog — tag_base + 28 tag classes + Catalog tuple
// ═══════════════════════════════════════════════════════════════════

using ::crucible::safety::diag::tag_base;

// 28 diagnostic tag classes.
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

// Catalog tuple + cardinality.
using ::crucible::safety::diag::Catalog;
using ::crucible::safety::diag::catalog_size;

// is_diagnostic_class_v + accessors.
template <typename T>
inline constexpr bool is_diagnostic_class_v =
    ::crucible::safety::diag::is_diagnostic_class_v<T>;

template <typename T>
inline constexpr std::string_view diagnostic_name_v =
    ::crucible::safety::diag::diagnostic_name_v<T>;

template <typename T>
inline constexpr std::string_view diagnostic_description_v =
    ::crucible::safety::diag::diagnostic_description_v<T>;

template <typename T>
inline constexpr std::string_view diagnostic_remediation_v =
    ::crucible::safety::diag::diagnostic_remediation_v<T>;

// Diagnostic<DiagnosticClass, Ctx...> wrapper + shape trait.
template <typename DiagnosticClass, typename... Context>
using Diagnostic = ::crucible::safety::diag::Diagnostic<DiagnosticClass, Context...>;

template <typename T>
inline constexpr bool is_diagnostic_v =
    ::crucible::safety::diag::is_diagnostic_v<T>;

// ═══════════════════════════════════════════════════════════════════
// Category enum + bidirectional map (tag_of_t / category_of_v)
// ═══════════════════════════════════════════════════════════════════

using ::crucible::safety::diag::Category;

// Category → tag type.
template <Category C>
using tag_of_t = ::crucible::safety::diag::tag_of_t<C>;

// Tag type → Category.
template <typename Tag>
inline constexpr Category category_of_v =
    ::crucible::safety::diag::category_of_v<Tag>;

// constexpr array of every Category in catalog order.
using ::crucible::safety::diag::categories_v;

// ═══════════════════════════════════════════════════════════════════
// StableName — type / function display-name + 64-bit fingerprint
// ═══════════════════════════════════════════════════════════════════

template <typename T>
inline constexpr std::string_view stable_name_of =
    ::crucible::safety::diag::stable_name_of<T>;

template <typename T>
inline constexpr std::uint64_t stable_type_id =
    ::crucible::safety::diag::stable_type_id<T>;

template <auto FnPtr>
inline constexpr std::uint64_t stable_function_id =
    ::crucible::safety::diag::stable_function_id<FnPtr>;

template <typename... Ts>
using canonicalize_pack_t =
    ::crucible::safety::diag::canonicalize_pack_t<Ts...>;

// FNV-1a constants (re-exported for fixy-side custom folds).
inline constexpr std::uint64_t FNV1A_OFFSET_BASIS =
    ::crucible::safety::diag::detail::FNV1A_OFFSET_BASIS;
inline constexpr std::uint64_t FNV1A_PRIME =
    ::crucible::safety::diag::detail::FNV1A_PRIME;

// ═══════════════════════════════════════════════════════════════════
// Insights — per-tag explanatory provider
// ═══════════════════════════════════════════════════════════════════

template <typename Tag>
using insight_provider = ::crucible::safety::diag::insight_provider<Tag>;

template <typename Tag>
using insights_quality_thresholds =
    ::crucible::safety::diag::insights_quality_thresholds<Tag>;

// ═══════════════════════════════════════════════════════════════════
// RowHashFold — canonical wrapper-nesting hash fold (FOUND-I02)
// ═══════════════════════════════════════════════════════════════════

template <typename T>
using row_hash_contribution =
    ::crucible::safety::diag::row_hash_contribution<T>;

template <typename T>
inline constexpr std::uint64_t row_hash_contribution_v =
    ::crucible::safety::diag::row_hash_contribution_v<T>;

inline constexpr std::uint64_t EMPTY_ROW_HASH =
    ::crucible::safety::diag::detail::EMPTY_ROW_HASH;

}  // namespace crucible::fixy::diag

// ── Self-test ──────────────────────────────────────────────────────
//
// Witness that every alias preserves substrate identity.  Full
// coverage in test_fixy_diag.cpp.

namespace crucible::fixy::diag::self_test {

// Category enum identity.
static_assert(Category::EffectRowMismatch ==
              ::crucible::safety::diag::Category::EffectRowMismatch);
static_assert(Category::LinearAliasViolation ==
              ::crucible::safety::diag::Category::LinearAliasViolation);

// catalog_size frozen at 31 — bumps require coordinated Catalog +
// Category + Diagnostic.h append-only edits.
static_assert(catalog_size == 31,
    "fixy::diag::catalog_size must match safety::diag::catalog_size");

// Tag-class identity.
static_assert(std::is_same_v<HotPathViolation,
                             ::crucible::safety::diag::HotPathViolation>,
    "fixy::diag::HotPathViolation must alias the substrate tag class");

// Diagnostic wrapper template identity.
static_assert(std::is_same_v<
    Diagnostic<HotPathViolation, int, float>,
    ::crucible::safety::diag::Diagnostic<
        ::crucible::safety::diag::HotPathViolation, int, float>>,
    "fixy::diag::Diagnostic must alias safety::diag::Diagnostic");

// Bidirectional map round-trip.
static_assert(category_of_v<HotPathViolation> == Category::HotPathViolation);
static_assert(std::is_same_v<tag_of_t<Category::HotPathViolation>,
                             HotPathViolation>);

// is_diagnostic_class_v witness.
static_assert(is_diagnostic_class_v<HotPathViolation>);
static_assert(!is_diagnostic_class_v<int>);
static_assert(!is_diagnostic_class_v<tag_base>);

// stable_name_of probe — value is a non-empty string for in-house type.
struct DiagSentinelStableName_TypeA {};
static_assert(!stable_name_of<DiagSentinelStableName_TypeA>.empty());

// EMPTY_ROW_HASH passes through.
static_assert(EMPTY_ROW_HASH ==
              ::crucible::safety::diag::detail::EMPTY_ROW_HASH);

}  // namespace crucible::fixy::diag::self_test
