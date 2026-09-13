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

// The exact cardinality is pinned beside the axis enumeration itself,
// which is where a new axis is added.  This file holds the floor only,
// which catches the opposite move: an axis enumerator and its catalog
// entry removed together.

static_assert(fd::fixy_catalog_size >= 22, "fixy_catalog_size fell below 22: an axis enumerator and its "
                                           "catalog entry were removed without updating the exact pin beside "
                                           "the enumeration as well as this floor.");

static_assert(std::tuple_size_v<fd::FixyCatalog> >= 22,
              "FixyCatalog holds fewer than 22 entries: the same removal, seen "
              "through the tuple size rather than the constant.");

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

struct DiagCatalogSentinel_NotATag {};
static_assert(!fd::is_fixy_diag_v<DiagCatalogSentinel_NotATag>);
static_assert(!fd::is_fixy_diag_v<int>);
static_assert(!fd::is_fixy_diag_v<float>);
static_assert(!fd::is_fixy_diag_v<void>);
static_assert(!fd::is_fixy_diag_v<cd::tag_base>);

// The two catalogs must stay disjoint.
static_assert(!fd::is_fixy_diag_v<cd::EffectRowMismatch>);
static_assert(!fd::is_fixy_diag_v<cd::HotPathViolation>);
static_assert(!fd::is_fixy_diag_v<cd::DetSafeLeak>);
static_assert(!fd::is_fixy_diag_v<cd::LinearityViolation>);
static_assert(!fd::is_fixy_diag_v<cd::LinearAliasViolation>);

static_assert(fd::axis_for_tag_v<fd::FixyNotEngaged_Type> == fx::dim::DimensionAxis::Type);
static_assert(fd::axis_for_tag_v<fd::FixyNotEngaged_Refinement> == fx::dim::DimensionAxis::Refinement);
static_assert(fd::axis_for_tag_v<fd::FixyNotEngaged_Usage> == fx::dim::DimensionAxis::Usage);
static_assert(fd::axis_for_tag_v<fd::FixyNotEngaged_Effect> == fx::dim::DimensionAxis::Effect);
static_assert(fd::axis_for_tag_v<fd::FixyNotEngaged_Security> == fx::dim::DimensionAxis::Security);
static_assert(fd::axis_for_tag_v<fd::FixyNotEngaged_Staleness> == fx::dim::DimensionAxis::Staleness);
static_assert(fd::axis_for_tag_v<fd::FixyNotEngaged_Regime> == fx::dim::DimensionAxis::Regime);

// The forward and reverse lookups must round-trip on every enumerator.
// The substrate checks this too, but repeating it here under the project
// warning flags catches include-order and alias drift.

inline constexpr bool kRoundTripHoldsForEveryAxis = []() consteval {
    bool ok = true;
    static constexpr auto axes =
        std::define_static_array(std::meta::enumerators_of(^^::crucible::safety::DimensionAxis));
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

static_assert(kRoundTripHoldsForEveryAxis, "Round-trip axis_for_tag_v(tag_for_axis_t<D>) must equal D "
                                           "for every DimensionAxis enumerator.");

// The catalog is ordered by the axis enumerator values.
static_assert(std::is_same_v<std::tuple_element_t<0, fd::FixyCatalog>, fd::FixyNotEngaged_Type>);
static_assert(std::is_same_v<std::tuple_element_t<1, fd::FixyCatalog>, fd::FixyNotEngaged_Refinement>);
static_assert(std::is_same_v<std::tuple_element_t<19, fd::FixyCatalog>, fd::FixyNotEngaged_Staleness>);
static_assert(std::is_same_v<std::tuple_element_t<20, fd::FixyCatalog>, fd::FixyNotEngaged_Synchronization>);
static_assert(std::is_same_v<std::tuple_element_t<21, fd::FixyCatalog>, fd::FixyNotEngaged_Regime>);

// These tags inherit the substrate's diagnostic base, so the substrate
// accessors must keep working on them.  A closed enumeration must not
// shadow the open tag-base machinery.

static_assert(cd::is_diagnostic_class_v<fd::FixyNotEngaged_Effect>,
              "Fixy tags inherit substrate tag_base and must satisfy "
              "is_diagnostic_class_v.");
static_assert(!cd::diagnostic_name_v<fd::FixyNotEngaged_Effect>.empty());
static_assert(!cd::diagnostic_description_v<fd::FixyNotEngaged_Effect>.empty());
static_assert(!cd::diagnostic_remediation_v<fd::FixyNotEngaged_Effect>.empty());

static_assert(cd::stable_type_id<fd::FixyNotEngaged_Type> != cd::stable_type_id<fd::FixyNotEngaged_Refinement>);
static_assert(cd::stable_type_id<fd::FixyNotEngaged_Effect> != cd::stable_type_id<fd::FixyNotEngaged_Security>);
static_assert(cd::stable_type_id<fd::FixyNotEngaged_Staleness> != cd::stable_type_id<fd::FixyNotEngaged_Type>);

// The three pairs above are spot checks.  The identifier is a 64-bit
// hash of the type's display string, so a collision among this many
// entries is vanishingly unlikely, but a rename could introduce one and
// a spot check would not see it.  Sorting the whole set and looking for
// an adjacent duplicate covers every pair, and it fires at the catalog
// rather than at the routing bug a collision would cause.

namespace fixy_id_distinct_witness {

template <std::size_t... Is>
[[nodiscard]] inline consteval std::array<std::uint64_t, fd::fixy_catalog_size>
collect_fixy_ids(std::index_sequence<Is...>) noexcept {
    return {cd::stable_type_id<std::tuple_element_t<Is, fd::FixyCatalog>>...};
}

[[nodiscard]] inline consteval bool every_fixy_tag_id_distinct() noexcept {
    auto ids = collect_fixy_ids(std::make_index_sequence<fd::fixy_catalog_size>{});
    std::sort(ids.begin(), ids.end());
    return std::adjacent_find(ids.begin(), ids.end()) == ids.end();
}

}  // namespace fixy_id_distinct_witness

static_assert(fixy_id_distinct_witness::every_fixy_tag_id_distinct(),
              "two FixyCatalog entries share one stable_type_id.  A collision "
              "among this many entries is near-zero by chance, so a rename "
              "introduced it.  Rename the offending tag to break the "
              "collision.");

// A tag description must not spell out how many axes there are.  The
// number goes stale the moment an axis ships, and a reader of the error
// is then misled about the real cardinality.  The fold walks every
// description and refuses any that carries a literal count.

namespace found_029_drift_witness {

template <std::size_t I>
[[nodiscard]] inline consteval bool description_lacks_hardcoded_count() noexcept {
    using Tag = std::tuple_element_t<I, fd::FixyCatalog>;
    constexpr std::string_view desc = Tag::description;
    return desc.find("29 dimensions") == std::string_view::npos && desc.find("33 dimensions") == std::string_view::npos
        && desc.find("29 axes") == std::string_view::npos && desc.find("33 axes") == std::string_view::npos
        && desc.find("twenty-nine") == std::string_view::npos && desc.find("thirty-three") == std::string_view::npos;
}

template <std::size_t... Is>
[[nodiscard]] inline consteval bool all_descriptions_clean(std::index_sequence<Is...>) noexcept {
    return (description_lacks_hardcoded_count<Is>() && ...);
}

}  // namespace found_029_drift_witness

static_assert(found_029_drift_witness::all_descriptions_clean(std::make_index_sequence<fd::fixy_catalog_size>{}),
              "a FixyNotEngaged_<Axis> tag's description carries a literal "
              "axis count ('29 dimensions', '33 axes', 'thirty-three', or "
              "similar).  Such a count drifts the moment a new axis ships.  Drop "
              "it from the tag macro and point the reader at "
              "`safety::DIMENSION_AXIS_COUNT` or "
              "`fixy::diag::kFixyCatalogDocstringCardinality` instead.");

// Type is the only axis with an implicitly injected engagement marker,
// so advice about dropping an explicit one is meaningless on any other
// axis.  The fold walks every duplicate-tag remediation outside Type and
// refuses any that carries that advice.

namespace found_031_drift_witness {

template <std::size_t I>
[[nodiscard]] inline consteval bool no_cross_axis_type_advice() noexcept {
    if constexpr (I == 0) {
        // Index zero is the Type axis, where the note belongs.  The
        // check looks for bleed onto the other axes.
        return true;
    } else {
        using Tag = std::tuple_element_t<I, fd::FixyDuplicateCatalog>;
        constexpr std::string_view rem = Tag::remediation;
        return rem.find("Drop the explicit Type marker") == std::string_view::npos
            && rem.find("Type marker would trigger") == std::string_view::npos
            && rem.find("fixy::fn implicitly engages Type") == std::string_view::npos;
    }
}

template <std::size_t... Is>
[[nodiscard]] inline consteval bool all_non_type_remediations_clean(std::index_sequence<Is...>) noexcept {
    return (no_cross_axis_type_advice<Is>() && ...);
}

}  // namespace found_031_drift_witness

static_assert(found_031_drift_witness::all_non_type_remediations_clean(
                  std::make_index_sequence<fd::fixy_duplicate_catalog_size>{}),
              "a non-Type FixyDuplicate_<Axis> tag's remediation carries "
              "Type-axis advice ('Drop the explicit Type marker', 'Type marker "
              "would trigger a duplicate', or 'fixy::fn implicitly engages "
              "Type').  Only the Type axis injects an engagement marker "
              "implicitly, so the advice fits nowhere else.  Use the two-argument "
              "CRUCIBLE_FIXY_DUPLICATE_TAG macro for every other axis, and "
              "CRUCIBLE_FIXY_DUPLICATE_TAG_EX only for Type, with the "
              "auto-injection note as its third argument.");

int main() { return 0; }
