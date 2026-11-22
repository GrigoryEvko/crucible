// Negative-compile test.  This file must fail to compile.
//
// Mismatch class 1 of 2 for the active region of CrucibleContext: an Option
// of a Ref of a region with no tag cannot become a
// `fixy::Tagged<Option<Ref<const RegionNode>>, tags::source::Vigil>`.
//
// The only doors into a Tagged are mint_tagged, which names the source
// at the call site, and retag along an admitted edge.  A region assigned
// straight into the field would carry the Vigil provenance without anyone
// having written it, so a region that the background worker of the Vigil
// never published could reach the active region of the context.
//
// Companion fixture: neg_active_region_ptr_cross_source_assignment.cpp
//   * That one refuses a region tagged with another source (laundering).
//   * This one refuses a region with no tag at all (bypass).

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
    using ActiveRegionLink = ::fixy::Tagged<RegionLink, ::fixy::tags::source::Vigil>;

    crucible::FakeRegionNode const region{};
    const RegionLink untagged = ::fixy::some(::fixy::erase_brand(::fixy::to(region)));

    // Should fail: no conversion from an untagged region to the tagged slot;
    // the provenance is written only through mint_tagged.
    ActiveRegionLink field = untagged;
    (void)field;
    return 0;
}
