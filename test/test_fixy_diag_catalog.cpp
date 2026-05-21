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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

namespace fd = crucible::fixy::diag;
namespace cd = crucible::safety::diag;
namespace fx = crucible::fixy;

// ─── 1. Cardinality ───────────────────────────────────────────────
//
// FIXY-U-128 / U-129 floor-vs-ceiling split: the EXACT ceiling pin
// for FixyCatalog cardinality is owned transitively by the substrate
// — `fixy_catalog_size == kDimAxisCount` in fixy/Reject.h:449 (the
// bijection witness) plus `DIMENSION_AXIS_COUNT == 22` in
// safety/DimensionTraits.h:600 colocated with the source-of-truth
// enum.  THIS TU only holds the FLOOR pin (`>= 22`) which catches
// the inverse direction — an accidental REMOVAL of a DimensionAxis
// enumerator (and its matching FixyCatalog entry).

static_assert(fd::fixy_catalog_size >= 22,
    "floor: fixy::diag::fixy_catalog_size regressed below 22 — a "
    "DimensionAxis enumerator (and its matching FixyCatalog entry) "
    "was removed without updating both DimensionTraits.h's colocated "
    "ceiling pin AND this floor witness.");

static_assert(std::tuple_size_v<fd::FixyCatalog> >= 22,
    "floor: FixyCatalog tuple-size regressed below 22 — same removal "
    "drift as above, expanded to the structural-identity form.");

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
static_assert(fd::is_fixy_diag_v<fd::FixyNotEngaged_Synchronization>);
static_assert(fd::is_fixy_diag_v<fd::FixyNotEngaged_Regime>);

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
static_assert(fd::axis_for_tag_v<fd::FixyNotEngaged_Regime>
              == fx::dim::DimensionAxis::Regime);

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
static_assert(std::is_same_v<
    std::tuple_element_t<20, fd::FixyCatalog>,
    fd::FixyNotEngaged_Synchronization>);
static_assert(std::is_same_v<
    std::tuple_element_t<21, fd::FixyCatalog>,
    fd::FixyNotEngaged_Regime>);

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

// ─── 10. fixy-M-13: exhaustive 22×21/2 = 231 pair-wise distinctness
//
// Section 9 spot-checks 3 of the 231 unordered pairs.  FNV-1a + fmix64
// over the type-display-string gives ~64 effective bits of entropy,
// so a collision in 22 entries is cryptographically near-zero today —
// but the spot-check would NOT catch a future tag rename that happens
// to hash-collide with an existing entry.  This fold builds the full
// std::array of stable_type_ids from FixyCatalog, sorts, then asserts
// adjacent_find finds nothing.  A regression fires at compile time at
// the catalog site, not at runtime when collision causes routing bugs.

namespace fixy_id_distinct_witness {

template <std::size_t... Is>
[[nodiscard]] inline consteval std::array<std::uint64_t,
                                          fd::fixy_catalog_size>
collect_fixy_ids(std::index_sequence<Is...>) noexcept
{
    return {cd::stable_type_id<
                std::tuple_element_t<Is, fd::FixyCatalog>>...};
}

[[nodiscard]] inline consteval bool every_fixy_tag_id_distinct() noexcept
{
    auto ids = collect_fixy_ids(
        std::make_index_sequence<fd::fixy_catalog_size>{});
    std::sort(ids.begin(), ids.end());
    return std::adjacent_find(ids.begin(), ids.end()) == ids.end();
}

}  // namespace fixy_id_distinct_witness

static_assert(fixy_id_distinct_witness::every_fixy_tag_id_distinct(),
    "fixy-M-13: two FixyCatalog entries share the same "
    "stable_type_id.  FNV-1a + fmix64 collisions in 22 entries are "
    "cryptographically near-zero, so this fires only on a rename "
    "regression that introduced a colliding tag name.  Resolve by "
    "renaming the offending tag to break the hash collision.");

int main() {
    return 0;
}
