// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Fixture 1 of 2 for the loaded-region door of deserialize_region.
//
// Premise: deserialize_region returns an optional region under the
// earned source::Loaded tag.  A caller unwraps the pointer with
// `->value()` after it has seen that a region came back.  The result
// never converts to a raw RegionNode* by itself.
//
// Distinct mismatch class from neg_deserialize_region_cross_tag.cpp:
//   * This fixture: the result converts to no raw pointer.
//   * Companion: a Loaded region does not pass as an External one.

#include <crucible/Serialize.h>

int main() {
    crucible::Arena arena{1024};
    std::span<const std::uint8_t> bytes{};

    // MUST fail: deserialize_region returns std::optional<LoadedRegionNode>.
    crucible::RegionNode* raw =
        crucible::deserialize_region(::foundation::effects::testing::test().alloc, bytes, arena);
    return raw == nullptr ? 0 : 1;
}
