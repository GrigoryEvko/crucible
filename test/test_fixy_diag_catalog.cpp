// ── test_fixy_diag_catalog — FixyCatalog + bijection sentinel ──────
//
// FIXY-AUDIT-C8 reconciliation surface.  Witnesses that the closed
// fixy diagnostic enumeration (`fixy::diag::FixyCatalog`) ships:
//
//   1. Exactly 20 entries — one per DimensionAxis enumerator.
//   2. The closed-set discriminator `is_fixy_diag_v<T>` returns true
//      for every catalog entry and false for substrate Catalog tags +
//      plain types + tag_base.
//   3. The reverse lookup `axis_for_tag_v<Tag>` is the round-trip
//      inverse of the forward `tag_for_axis_t<Axis>`.
//   4. The substrate Catalog and FixyCatalog are disjoint at every
//      shipped substrate index (a sanity check that the two
//      enumerations never accidentally overlap).
//
// Companion neg-compile fixtures live in test/fixy_neg/:
//   - neg_fixy_diag_catalog_substrate_tag.cpp
//   - neg_fixy_diag_catalog_plain_type.cpp
//
// Together: HS14 floor for the new C8 surface.

#include <crucible/fixy/Diag.h>
#include <crucible/fixy/Reject.h>

#include <tuple>
#include <type_traits>

namespace fd = crucible::fixy::diag;
namespace cd = crucible::safety::diag;
namespace fx = crucible::fixy;

// ─── 1. Cardinality ───────────────────────────────────────────────

static_assert(fd::fixy_catalog_size == 20,
    "FixyCatalog must enumerate exactly 20 entries — one per "
    "DimensionAxis enumerator.");

static_assert(std::tuple_size_v<fd::FixyCatalog> == 20);

// ─── 2. is_fixy_diag_v on every catalog entry ─────────────────────

static_assert(fd::is_fixy_diag_v<fd::FixyNotEngaged_Type>);
static_assert(fd::is_fixy_diag_v<fd::FixyNotEngaged_Refinement>);
static_assert(fd::is_fixy_diag_v<fd::FixyNotEngaged_Usage>);
static_assert(fd::is_fixy_diag_v<fd::FixyNotEngaged_Effect>);
static_assert(fd::is_fixy_diag_v<fd::FixyNotEngaged_Security>);
static_assert(fd::is_fixy_diag_v<fd::FixyNotEngaged_Protocol>);
static_assert(fd::is_fixy_diag_v<fd::FixyNotEngaged_Lifetime>);
static_assert(fd::is_fixy_diag_v<fd::FixyNotEngaged_Provenance>);
static_assert(fd::is_fixy_diag_v<fd::FixyNotEngaged_Trust>);
static_assert(fd::is_fixy_diag_v<fd::FixyNotEngaged_Representation>);
static_assert(fd::is_fixy_diag_v<fd::FixyNotEngaged_Observability>);
static_assert(fd::is_fixy_diag_v<fd::FixyNotEngaged_Complexity>);
static_assert(fd::is_fixy_diag_v<fd::FixyNotEngaged_Precision>);
static_assert(fd::is_fixy_diag_v<fd::FixyNotEngaged_Space>);
static_assert(fd::is_fixy_diag_v<fd::FixyNotEngaged_Overflow>);
static_assert(fd::is_fixy_diag_v<fd::FixyNotEngaged_Mutation>);
static_assert(fd::is_fixy_diag_v<fd::FixyNotEngaged_Reentrancy>);
static_assert(fd::is_fixy_diag_v<fd::FixyNotEngaged_Size>);
static_assert(fd::is_fixy_diag_v<fd::FixyNotEngaged_Version>);
static_assert(fd::is_fixy_diag_v<fd::FixyNotEngaged_Staleness>);

// ─── 3. is_fixy_diag_v rejects non-catalog types ──────────────────

struct DiagCatalogSentinel_NotATag {};
static_assert(!fd::is_fixy_diag_v<DiagCatalogSentinel_NotATag>);
static_assert(!fd::is_fixy_diag_v<int>);
static_assert(!fd::is_fixy_diag_v<float>);
static_assert(!fd::is_fixy_diag_v<void>);
static_assert(!fd::is_fixy_diag_v<cd::tag_base>);

// ─── 4. Substrate Catalog ∩ FixyCatalog == ∅ ──────────────────────

static_assert(!fd::is_fixy_diag_v<cd::EffectRowMismatch>);
static_assert(!fd::is_fixy_diag_v<cd::HotPathViolation>);
static_assert(!fd::is_fixy_diag_v<cd::DetSafeLeak>);
static_assert(!fd::is_fixy_diag_v<cd::LinearityViolation>);
static_assert(!fd::is_fixy_diag_v<cd::LinearAliasViolation>);

// ─── 5. axis_for_tag round-trip ───────────────────────────────────

static_assert(fd::axis_for_tag_v<fd::FixyNotEngaged_Type>
              == fx::dim::DimensionAxis::Type);
static_assert(fd::axis_for_tag_v<fd::FixyNotEngaged_Refinement>
              == fx::dim::DimensionAxis::Refinement);
static_assert(fd::axis_for_tag_v<fd::FixyNotEngaged_Usage>
              == fx::dim::DimensionAxis::Usage);
static_assert(fd::axis_for_tag_v<fd::FixyNotEngaged_Effect>
              == fx::dim::DimensionAxis::Effect);
static_assert(fd::axis_for_tag_v<fd::FixyNotEngaged_Security>
              == fx::dim::DimensionAxis::Security);
static_assert(fd::axis_for_tag_v<fd::FixyNotEngaged_Staleness>
              == fx::dim::DimensionAxis::Staleness);

// ─── 6. Round-trip via reflection over every DimensionAxis ────────
//
// For each enumerator, axis_for_tag_v(tag_for_axis_t<D>) == D.  This
// is checked inside Reject.h's bijection self-test, but witnessing it
// in the sentinel TU under project warning flags guards against
// regressions in include-order or alias drift.

inline constexpr bool kRoundTripHoldsForEveryAxis = []() consteval {
    bool ok = true;
    static constexpr auto axes = std::define_static_array(
        std::meta::enumerators_of(^^::crucible::safety::DimensionAxis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : axes) {
        constexpr auto axis_v = [:en:];
        if (fd::axis_for_tag_v<fd::tag_for_axis_t<axis_v>> != axis_v) {
            ok = false;
        }
    }
#pragma GCC diagnostic pop
    return ok;
}();

static_assert(kRoundTripHoldsForEveryAxis,
    "Round-trip axis_for_tag_v(tag_for_axis_t<D>) must equal D "
    "for every DimensionAxis enumerator.");

// ─── 7. FixyCatalog ordering follows DimensionAxis value ordering ─

static_assert(std::is_same_v<
    std::tuple_element_t<0, fd::FixyCatalog>,
    fd::FixyNotEngaged_Type>);
static_assert(std::is_same_v<
    std::tuple_element_t<1, fd::FixyCatalog>,
    fd::FixyNotEngaged_Refinement>);
static_assert(std::is_same_v<
    std::tuple_element_t<19, fd::FixyCatalog>,
    fd::FixyNotEngaged_Staleness>);

// ─── 8. Substrate accessors still work on fixy tags ───────────────
//
// FixyNotEngaged_* tags inherit safety::diag::tag_base, so they
// participate in the substrate's diagnostic_name_v / Diagnostic<>
// accessors.  Witness that the closed-fixy enumeration does NOT
// shadow the open-tag-base machinery.

static_assert(cd::is_diagnostic_class_v<fd::FixyNotEngaged_Effect>,
    "Fixy tags inherit substrate tag_base and must satisfy "
    "is_diagnostic_class_v.");
static_assert(!cd::diagnostic_name_v<fd::FixyNotEngaged_Effect>.empty());
static_assert(!cd::diagnostic_description_v<fd::FixyNotEngaged_Effect>.empty());
static_assert(!cd::diagnostic_remediation_v<fd::FixyNotEngaged_Effect>.empty());

// ─── 9. Runtime sanity — every fixy tag has a distinct stable_type_id

static_assert(cd::stable_type_id<fd::FixyNotEngaged_Type>
              != cd::stable_type_id<fd::FixyNotEngaged_Refinement>);
static_assert(cd::stable_type_id<fd::FixyNotEngaged_Effect>
              != cd::stable_type_id<fd::FixyNotEngaged_Security>);
static_assert(cd::stable_type_id<fd::FixyNotEngaged_Staleness>
              != cd::stable_type_id<fd::FixyNotEngaged_Type>);

int main() {
    return 0;
}
