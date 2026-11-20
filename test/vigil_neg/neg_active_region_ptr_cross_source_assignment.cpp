// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Mismatch class 2 of 2 for the active region of CrucibleContext: a region
// tagged `tags::source::Arena` cannot become one tagged
// `tags::source::Vigil` without a retag along an admitted edge, and the
// catalog admits none between these two sources.
//
// An arena-owned region is freed when its arena resets.  If it could
// take the Vigil tag by assignment, the active region would dangle as
// soon as the arena reset, while its type claimed the lifetime of a
// region that the background worker of the Vigil published.
//
// Companion fixture: neg_active_region_ptr_raw_assignment.cpp
//   * That one refuses a region with no tag at all (bypass).
//   * This one refuses a region tagged with another source (laundering).

#include <fixy/Core.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>

namespace crucible {
struct FakeRegionNode {
    int dummy;
};
}  // namespace crucible

int main() {
    using RegionLink = ::fixy::Option<::fixy::Ref<const crucible::FakeRegionNode>>;
    using VigilRegion = ::fixy::Tagged<RegionLink, ::fixy::tags::source::Vigil>;

    crucible::FakeRegionNode const region{};
    auto arena_tagged = ::fixy::mint_tagged<::fixy::tags::source::Arena>(
        RegionLink{::fixy::some(::fixy::erase_brand(::fixy::to(region)))});

    // Should FAIL: the two sources are distinct types, and no edge of the
    // retag catalog carries a value from Arena to Vigil.
    VigilRegion field = arena_tagged;
    (void)field;
    return 0;
}
