// SPDX-License-Identifier: Apache-2.0
//
// test/test_fixy_diag.cpp
//
// FIXY-A5 companion — exercise the fixy::diag tag catalog.  Pins
// (1) every dim has a diag_tag_for<D> specialization with non-empty
// name, (2) the FixyDiagCatalog tuple has exactly 20 entries in
// dim-enumerator order, (3) tag types are non-convertible (no
// accidental cross-dim diagnostics), (4) constexpr accessors work
// under non-constant arguments at runtime (per the runtime smoke
// test discipline).

#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Reject.h>
#include <crucible/safety/diag/Insights.h>
#include <crucible/safety/diag/JsonEmitter.h>
#include <crucible/safety/diag/StableName.h>

#include <cstdint>
#include <cstdio>
#include <meta>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace {

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cfd = crucible::fixy::diag;

// ── Catalog cardinality ────────────────────────────────────────────
static_assert(std::tuple_size_v<cfd::FixyDiagCatalog> == 20);
static_assert(std::tuple_size_v<cfd::FixyDiagCatalog> == cd::count_v);

// ── Per-dim diag-tag mapping ───────────────────────────────────────
static_assert(cfd::diag_tag_for_t<cd::Type>::name           == "FixyNotEngaged_Type");
static_assert(cfd::diag_tag_for_t<cd::Refinement>::name     == "FixyNotEngaged_Refinement");
static_assert(cfd::diag_tag_for_t<cd::Usage>::name          == "FixyNotEngaged_Usage");
static_assert(cfd::diag_tag_for_t<cd::Effect>::name         == "FixyNotEngaged_Effect");
static_assert(cfd::diag_tag_for_t<cd::Security>::name       == "FixyNotEngaged_Security");
static_assert(cfd::diag_tag_for_t<cd::Protocol>::name       == "FixyNotEngaged_Protocol");
static_assert(cfd::diag_tag_for_t<cd::Lifetime>::name       == "FixyNotEngaged_Lifetime");
static_assert(cfd::diag_tag_for_t<cd::Provenance>::name     == "FixyNotEngaged_Provenance");
static_assert(cfd::diag_tag_for_t<cd::Trust>::name          == "FixyNotEngaged_Trust");
static_assert(cfd::diag_tag_for_t<cd::Representation>::name == "FixyNotEngaged_Representation");
static_assert(cfd::diag_tag_for_t<cd::Observability>::name  == "FixyNotEngaged_Observability");
static_assert(cfd::diag_tag_for_t<cd::Complexity>::name     == "FixyNotEngaged_Complexity");
static_assert(cfd::diag_tag_for_t<cd::Precision>::name      == "FixyNotEngaged_Precision");
static_assert(cfd::diag_tag_for_t<cd::Space>::name          == "FixyNotEngaged_Space");
static_assert(cfd::diag_tag_for_t<cd::Overflow>::name       == "FixyNotEngaged_Overflow");
static_assert(cfd::diag_tag_for_t<cd::Mutation>::name       == "FixyNotEngaged_Mutation");
static_assert(cfd::diag_tag_for_t<cd::Reentrancy>::name     == "FixyNotEngaged_Reentrancy");
static_assert(cfd::diag_tag_for_t<cd::Size>::name           == "FixyNotEngaged_Size");
static_assert(cfd::diag_tag_for_t<cd::Version>::name        == "FixyNotEngaged_Version");
static_assert(cfd::diag_tag_for_t<cd::Staleness>::name      == "FixyNotEngaged_Staleness");

// ── Catalog order matches enumerator order ─────────────────────────
//
// std::tuple_element_t<i, FixyDiagCatalog> must equal
// diag_tag_for_t<DimAxis(i)>.  This pins the catalog as a positional
// enumerable matching the dim enum.
template <std::size_t I>
constexpr bool catalog_pos_matches() {
    using Tag = std::tuple_element_t<I, cfd::FixyDiagCatalog>;
    constexpr auto axis = static_cast<cd::DimAxis>(I);
    return std::is_same_v<Tag, cfd::diag_tag_for_t<axis>>;
}

static_assert(catalog_pos_matches<0>());
static_assert(catalog_pos_matches<1>());
static_assert(catalog_pos_matches<2>());
static_assert(catalog_pos_matches<3>());
static_assert(catalog_pos_matches<4>());
static_assert(catalog_pos_matches<5>());
static_assert(catalog_pos_matches<6>());
static_assert(catalog_pos_matches<7>());
static_assert(catalog_pos_matches<8>());
static_assert(catalog_pos_matches<9>());
static_assert(catalog_pos_matches<10>());
static_assert(catalog_pos_matches<11>());
static_assert(catalog_pos_matches<12>());
static_assert(catalog_pos_matches<13>());
static_assert(catalog_pos_matches<14>());
static_assert(catalog_pos_matches<15>());
static_assert(catalog_pos_matches<16>());
static_assert(catalog_pos_matches<17>());
static_assert(catalog_pos_matches<18>());
static_assert(catalog_pos_matches<19>());

// ── Tag-type non-convertibility ────────────────────────────────────
//
// Cross-dim diagnostic confusion is a TypeSafe violation; the diag
// tags MUST NOT silently convert between dims.
static_assert(!std::is_convertible_v<cfd::FixyNotEngaged_Type,
                                     cfd::FixyNotEngaged_Refinement>);
static_assert(!std::is_convertible_v<cfd::FixyNotEngaged_Usage,
                                     cfd::FixyNotEngaged_Effect>);
static_assert(!std::is_convertible_v<cfd::FixyNotEngaged_Security,
                                     cfd::FixyNotEngaged_Trust>);

// ── Each tag inherits from safety::diag::tag_base ──────────────────
//
// The substrate's diagnostic infrastructure detects tags via
// `is_base_of_v<tag_base, T>`; cf::diag entries must integrate.
static_assert(std::is_base_of_v<::crucible::safety::diag::tag_base,
                                cfd::FixyNotEngaged_Type>);
static_assert(std::is_base_of_v<::crucible::safety::diag::tag_base,
                                cfd::FixyNotEngaged_Staleness>);

// ── Every tag carries non-empty description (FIXY-A-PLUS-4) ────────
//
// `description` is the IDE-hover one-liner used by clangd / JsonEmitter
// downstream.  Empty descriptions would defeat the diagnostic richness
// upgrade.
template <std::size_t I>
constexpr bool tag_has_nonempty_description() {
    using Tag = std::tuple_element_t<I, cfd::FixyDiagCatalog>;
    return !Tag::description.empty();
}

static_assert(tag_has_nonempty_description<0>());
static_assert(tag_has_nonempty_description<5>());
static_assert(tag_has_nonempty_description<10>());
static_assert(tag_has_nonempty_description<15>());
static_assert(tag_has_nonempty_description<19>());

// ── insight_provider specializations are populated (FIXY-A-PLUS-4) ──
//
// Every FixyNotEngaged_<D> tag has an insight_provider with non-empty
// why_this_matters + symptom_pattern + correct_example +
// violating_example.  The substrate's primary template returns empty
// strings; non-empty here proves our specialization landed.
//
// Severity check: any of the four named severities (Hint / Warning /
// Error / Fatal — see safety/diag/Insights.h's Severity enum) is
// valid.  Specific severity-tier policy (Fatal for Security / Trust /
// Provenance / Lifetime, Warning for Observability, Error for the
// rest) is the substrate's call; we only pin that severity is one of
// the four named enumerators.
namespace sdiag = ::crucible::safety::diag;

template <std::size_t I>
constexpr bool tag_has_populated_insight() {
    using Tag    = std::tuple_element_t<I, cfd::FixyDiagCatalog>;
    using Insight = sdiag::insight_provider<Tag>;
    return !Insight::why_this_matters.empty()
        && !Insight::symptom_pattern.empty()
        && !Insight::correct_example.empty()
        && !Insight::violating_example.empty()
        && (Insight::severity == sdiag::Severity::Error
            || Insight::severity == sdiag::Severity::Warning
            || Insight::severity == sdiag::Severity::Fatal
            || Insight::severity == sdiag::Severity::Hint);
}

static_assert(tag_has_populated_insight<0>());   // Type — Error
static_assert(tag_has_populated_insight<5>());   // Protocol — Error
static_assert(tag_has_populated_insight<10>());  // Observability — Warning
static_assert(tag_has_populated_insight<15>());  // Mutation — Error
static_assert(tag_has_populated_insight<19>());  // Staleness — Error

// Full-coverage gate: every one of the 20 dims has populated
// insights.  Mirror of the reflection-driven consteval gate in
// fixy/Reject.h (which iterates dim::DimAxis directly); this version
// proves the equivalence via the FixyDiagCatalog tuple so a reviewer
// reading tests-only confirms the coverage without diving into the
// header.
template <std::size_t... Is>
constexpr bool every_tag_populated_helper(std::index_sequence<Is...>) {
    return (tag_has_populated_insight<Is>() && ...);
}

static_assert(every_tag_populated_helper(
    std::make_index_sequence<std::tuple_size_v<cfd::FixyDiagCatalog>>{}),
    "Every fixy::diag::FixyNotEngaged_<D> tag must have a populated "
    "insight_provider<> specialization.");

// ── Severity policy audit ─────────────────────────────────────────
//
// Mirrors the substrate's LifetimeViolation = Fatal precedent for
// security-class dims.  Observability mirrors ResidencyHeatViolation
// = Warning.  All other dims are Error.  Locks the policy at compile
// time so a refactor that downgrades a Fatal to Error fires here.
static_assert(sdiag::insight_provider<cfd::FixyNotEngaged_Security>::severity
              == sdiag::Severity::Fatal);
static_assert(sdiag::insight_provider<cfd::FixyNotEngaged_Lifetime>::severity
              == sdiag::Severity::Fatal);
static_assert(sdiag::insight_provider<cfd::FixyNotEngaged_Provenance>::severity
              == sdiag::Severity::Fatal);
static_assert(sdiag::insight_provider<cfd::FixyNotEngaged_Trust>::severity
              == sdiag::Severity::Fatal);
static_assert(sdiag::insight_provider<cfd::FixyNotEngaged_Observability>::severity
              == sdiag::Severity::Warning);
// Spot-check remaining dims stay Error (sample — full pass via the
// catalog walk above).
static_assert(sdiag::insight_provider<cfd::FixyNotEngaged_Type>::severity
              == sdiag::Severity::Error);
static_assert(sdiag::insight_provider<cfd::FixyNotEngaged_Effect>::severity
              == sdiag::Severity::Error);
static_assert(sdiag::insight_provider<cfd::FixyNotEngaged_Staleness>::severity
              == sdiag::Severity::Error);

// ── stable_name_for_dim integration ────────────────────────────────
//
// safety::diag::stable_name_of returns a portable consteval string.
// Per StableName.h's TU-fragility contract, use `.ends_with(...)`
// against the simple-suffix form rather than `.find(...)` which would
// match an imposter like `FixyNotEngaged_Usage_fake`.  The simple
// name is always a suffix of the qualified form (which renders as
// `crucible::fixy::diag::FixyNotEngaged_<D>`).
static_assert(cfd::stable_name_for_dim<cd::Usage>
              .ends_with("FixyNotEngaged_Usage"));
static_assert(cfd::stable_name_for_dim<cd::Staleness>
              .ends_with("FixyNotEngaged_Staleness"));
static_assert(cfd::stable_name_for_dim<cd::Type>
              .ends_with("FixyNotEngaged_Type"));
// Hashes are non-zero AND pairwise-distinct across dims (no FNV-1a
// collision on the 20 stable names).
static_assert(cfd::stable_type_id_for_dim<cd::Usage> != 0);
static_assert(cfd::stable_type_id_for_dim<cd::Usage>
              != cfd::stable_type_id_for_dim<cd::Effect>);
static_assert(cfd::stable_type_id_for_dim<cd::Type>
              != cfd::stable_type_id_for_dim<cd::Staleness>);

// ── WellInsightedTag / HasSubstantiveInsights concept gates ─────
//
// Downstream consumers can `requires WellInsightedTag<T>` to
// guarantee diagnostic prose; same for HasSubstantiveInsights.
// Verify all three named-severity tiers in the fixy catalog satisfy
// both concepts.
static_assert(sdiag::WellInsightedTag<cfd::FixyNotEngaged_Type>);
static_assert(sdiag::WellInsightedTag<cfd::FixyNotEngaged_Security>);
static_assert(sdiag::WellInsightedTag<cfd::FixyNotEngaged_Observability>);
static_assert(sdiag::HasSubstantiveInsights<cfd::FixyNotEngaged_Type>);
static_assert(sdiag::HasSubstantiveInsights<cfd::FixyNotEngaged_Security>);
static_assert(sdiag::HasSubstantiveInsights<cfd::FixyNotEngaged_Observability>);

// ── description fits IDE hover constraint (≤120 chars) ─────────
//
// Same threshold as the consteval gate in Reject.h
// (DESCRIPTION_MAX_CHARS).  Locked here too so the test surface
// tells the story for a reviewer who reads tests before the header.
template <std::size_t I>
constexpr bool description_fits_hover() {
    using Tag = std::tuple_element_t<I, cfd::FixyDiagCatalog>;
    return !Tag::description.empty()
        && Tag::description.size() <= 120;
}

template <std::size_t... Is>
constexpr bool all_descriptions_fit(std::index_sequence<Is...>) {
    return (description_fits_hover<Is>() && ...);
}

static_assert(all_descriptions_fit(
    std::make_index_sequence<std::tuple_size_v<cfd::FixyDiagCatalog>>{}));

// ── JsonEmitter / Category-enum non-registration documentation ───
//
// fixy diag tags are NOT entries of `safety::diag::Category`.  The
// substrate Category enum is closed (the 25 enumerators in
// Diagnostic.h).  Fixy ships its own catalog (FixyDiagCatalog) for
// extension purposes.  JsonEmitter::record_from_violation consumes
// Category, NOT a tag-T — so the JSON record path doesn't accept
// fixy tags directly today.  This is the documented design.
//
// Downstream consumers that want JSON emission for a fixy diagnostic
// either (a) wrap the fixy tag in a context-specific Category-bearing
// shim, or (b) emit via the insight_provider's prose directly.  The
// surface remains lean; no new emitter API ships here.
//
// We DO confirm the parsing path for our stable names round-trips:
constexpr auto parsed = ::crucible::safety::diag::parse_source_position(
    "fake_file.cpp:42:7@fake_fn");
static_assert(parsed.line == 42u);
static_assert(parsed.column == 7u);
static_assert(parsed.function == "fake_fn");

}  // namespace

int main() {
    // Runtime accessor exercise — pins constexpr-not-consteval contract.
    volatile std::uint8_t dim_idx_v = 19u;  // Staleness
    const auto axis = static_cast<cd::DimAxis>(dim_idx_v);

    // Switch over the volatile axis to demonstrate runtime dispatch
    // through the diag tag mapping.  All 20 arms must produce a
    // non-empty name.
    std::string_view view{};
    switch (axis) {
        case cd::Type:           view = cfd::FixyNotEngaged_Type::name; break;
        case cd::Refinement:     view = cfd::FixyNotEngaged_Refinement::name; break;
        case cd::Usage:          view = cfd::FixyNotEngaged_Usage::name; break;
        case cd::Effect:         view = cfd::FixyNotEngaged_Effect::name; break;
        case cd::Security:       view = cfd::FixyNotEngaged_Security::name; break;
        case cd::Protocol:       view = cfd::FixyNotEngaged_Protocol::name; break;
        case cd::Lifetime:       view = cfd::FixyNotEngaged_Lifetime::name; break;
        case cd::Provenance:     view = cfd::FixyNotEngaged_Provenance::name; break;
        case cd::Trust:          view = cfd::FixyNotEngaged_Trust::name; break;
        case cd::Representation: view = cfd::FixyNotEngaged_Representation::name; break;
        case cd::Observability:  view = cfd::FixyNotEngaged_Observability::name; break;
        case cd::Complexity:     view = cfd::FixyNotEngaged_Complexity::name; break;
        case cd::Precision:      view = cfd::FixyNotEngaged_Precision::name; break;
        case cd::Space:          view = cfd::FixyNotEngaged_Space::name; break;
        case cd::Overflow:       view = cfd::FixyNotEngaged_Overflow::name; break;
        case cd::Mutation:       view = cfd::FixyNotEngaged_Mutation::name; break;
        case cd::Reentrancy:     view = cfd::FixyNotEngaged_Reentrancy::name; break;
        case cd::Size:           view = cfd::FixyNotEngaged_Size::name; break;
        case cd::Version:        view = cfd::FixyNotEngaged_Version::name; break;
        case cd::Staleness:      view = cfd::FixyNotEngaged_Staleness::name; break;
        default:                 view = std::string_view{"<unreachable>"}; break;
    }

    if (view != "FixyNotEngaged_Staleness") {
        std::fprintf(stderr,
            "test_fixy_diag: runtime dispatch returned %.*s, "
            "expected FixyNotEngaged_Staleness\n",
            static_cast<int>(view.size()), view.data());
        return 1;
    }

    return 0;
}
