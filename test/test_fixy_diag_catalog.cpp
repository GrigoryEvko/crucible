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
static_assert(fd::is_fixy_diag_v<fd::FixyNotEngaged_FpMode>);
static_assert(fd::is_fixy_diag_v<fd::FixyNotEngaged_SyscallSurface>);
static_assert(fd::is_fixy_diag_v<fd::FixyNotEngaged_ControlFlow>);
static_assert(fd::is_fixy_diag_v<fd::FixyNotEngaged_CallShape>);
static_assert(fd::is_fixy_diag_v<fd::FixyNotEngaged_StackUse>);
static_assert(fd::is_fixy_diag_v<fd::FixyNotEngaged_GlobalState>);
static_assert(fd::is_fixy_diag_v<fd::FixyNotEngaged_Stdio>);

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

// ─── FIXY-FOUND-029: description-prose drift guard ────────────────
//
// Pre-FOUND-029, the `CRUCIBLE_FIXY_NOT_ENGAGED_TAG` macro's
// `description` field hardcoded the substring "29 dimensions",
// expanded into every FixyNotEngaged_<Axis> tag.  By the time the
// DimensionAxis universe reached 33 enumerators (per FIXY-V-088 /
// V-097 / V-238 / V-253 / V-266 extensions), the prose was stale by
// 4 axes — a reviewer reading the error would be misled about the
// true cardinality.
//
// FOUND-029 closure: rewrote the macro's prose to refer the reader
// to `safety::DIMENSION_AXIS_COUNT` + `kFixyCatalogDocstringCardinality`
// rather than embed any literal count.  The fold below is the
// regression witness — it walks every FixyCatalog entry's description
// at consteval and asserts no entry contains a hardcoded-count
// substring.  Adding "29 dimensions" / "33 dimensions" / etc. to ANY
// tag's description (via accidental copy-paste during a future macro
// rewrite) fires this assertion immediately at the tag-definition TU.

namespace found_029_drift_witness {

template <std::size_t I>
[[nodiscard]] inline consteval bool description_lacks_hardcoded_count() noexcept
{
    using Tag = std::tuple_element_t<I, fd::FixyCatalog>;
    constexpr std::string_view desc = Tag::description;
    return desc.find("29 dimensions") == std::string_view::npos
        && desc.find("33 dimensions") == std::string_view::npos
        && desc.find("29 axes")       == std::string_view::npos
        && desc.find("33 axes")       == std::string_view::npos
        && desc.find("twenty-nine")   == std::string_view::npos
        && desc.find("thirty-three")  == std::string_view::npos;
}

template <std::size_t... Is>
[[nodiscard]] inline consteval bool all_descriptions_clean(
    std::index_sequence<Is...>) noexcept
{
    return (description_lacks_hardcoded_count<Is>() && ...);
}

}  // namespace found_029_drift_witness

static_assert(found_029_drift_witness::all_descriptions_clean(
    std::make_index_sequence<fd::fixy_catalog_size>{}),
    "FIXY-FOUND-029: a FixyNotEngaged_<Axis> tag's description "
    "reintroduced a hardcoded axis-count phrase ('29 dimensions', "
    "'33 dimensions', 'twenty-nine', 'thirty-three', or similar).  "
    "These are forbidden because they drift the moment a new axis "
    "ships.  Per the macro at fixy/Reject.h:110 CRUCIBLE_FIXY_NOT_"
    "ENGAGED_TAG, drop the count entirely and refer the reader to "
    "the source-of-truth constants instead "
    "(`safety::DIMENSION_AXIS_COUNT` or "
    "`fixy::diag::kFixyCatalogDocstringCardinality`).");

int main() {
    return 0;
}
