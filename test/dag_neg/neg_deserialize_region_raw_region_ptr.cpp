// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Fixture 1 of 2 for the loaded-region door of deserialize_region.
//
// Premise: deserialize_region returns a Result of a region under the
// earned source::Loaded tag, or of the reason of a refusal.  A caller reads
// the Ref of the region with value() after it has seen that a region came
// back.  The region never converts to a raw RegionNode* by itself.
//
// Distinct mismatch class from neg_deserialize_region_cross_tag.cpp:
//   * This fixture: the result converts to no raw pointer.
//   * Companion: a Loaded region does not pass as an External one.

#include <crucible/Serialize.h>
#include <foundation/Quarantine.h>

int main() {
    ::fixy::BumpArena arena{1024};
    ::fixy::View<const std::byte> bytes{};

    // MUST fail: the region of the Result is a LoadedRegionNode.
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture reads a typed result into a raw pointer")
    crucible::RegionNode* raw = crucible::deserialize_region(::foundation::effects::testing::test().alloc, bytes, arena)
                                    .expect("the fixture names the type of a loaded region");
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    return raw == nullptr ? 0 : 1;
}
