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
