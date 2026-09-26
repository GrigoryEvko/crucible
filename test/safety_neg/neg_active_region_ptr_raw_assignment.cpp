// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Mismatch class 1 of 2 for the active region pointer of
// CrucibleContext: a raw `const RegionNode*` cannot become a
// `fixy::Tagged<const RegionNode*, tags::source::Vigil>`.
//
// The only doors into a Tagged are mint_tagged, which names the source
// at the call site, and retag along an admitted edge.  A raw pointer
// assigned straight into the field would carry the Vigil provenance
// without anyone having written it, so a region that Vigil's background
// worker never published could reach the context's active region.
//
// Companion fixture: neg_active_region_ptr_cross_source_assignment.cpp
//   * That one refuses a pointer tagged with another source (laundering).
//   * This one refuses a pointer with no tag at all (bypass).

#include <fixy/Tagged.h>
#include <fixy/Tags.h>

namespace crucible {
struct FakeRegionNode {
    int dummy;
};
}  // namespace crucible

int main() {
    using ActiveRegionPtr = ::fixy::Tagged<const crucible::FakeRegionNode*, ::fixy::tags::source::Vigil>;

    crucible::FakeRegionNode region{};
    const crucible::FakeRegionNode* raw_ptr = &region;

    // Should FAIL: no conversion from a raw pointer to the tagged
    // pointer; the provenance is written only through mint_tagged.
    ActiveRegionPtr field = raw_ptr;
    (void)field;
    return 0;
}
