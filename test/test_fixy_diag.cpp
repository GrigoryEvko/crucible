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

static_assert(fd::catalog_size == cd::catalog_size);
static_assert(fd::catalog_size == 31);

static_assert(std::is_same_v<fd::Catalog, cd::Catalog>);

// ─── 2. Bidirectional map round-trip ──────────────────────────────

static_assert(fd::category_of_v<fd::HotPathViolation>   == fd::Category::HotPathViolation);
static_assert(fd::category_of_v<fd::DetSafeLeak>        == fd::Category::DetSafeLeak);
static_assert(fd::category_of_v<fd::EffectRowMismatch>  == fd::Category::EffectRowMismatch);

static_assert(std::is_same_v<fd::tag_of_t<fd::Category::HotPathViolation>,
                             fd::HotPathViolation>);
static_assert(std::is_same_v<fd::tag_of_t<fd::Category::EffectRowMismatch>,
                             fd::EffectRowMismatch>);

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

// ─── 9. Runtime sanity — emit a Category through the alias ────────

int main() {
    fd::Category cat = fd::category_of_v<fd::HotPathViolation>;
    switch (cat) {
        case fd::Category::HotPathViolation: return 0;
        default:                              return 1;
    }
}
