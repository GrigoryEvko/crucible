// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for #1017 WRAP-Serialize-8
// (deserialize_region raw RegionNode* -> Tagged<RegionNode*, source::Loaded>).
//
// Premise: deserialized regions carry source::Loaded provenance.  A
// caller may explicitly unwrap at a legacy pointer boundary with
// `.value()`, but deserialize_region itself must not silently hand out
// a raw RegionNode*.
//
// Distinct mismatch class from neg_deserialize_region_cross_tag.cpp:
//   * This fixture: Tagged -> raw RegionNode* implicit extraction rejected.
//   * Companion: Loaded -> External cross-tag assignment rejected.

#include <crucible/Serialize.h>

int main() {
  crucible::Arena arena{1024};
  std::span<const std::uint8_t> bytes{};

  // MUST fail: deserialize_region returns LoadedRegionNode.
  crucible::RegionNode* raw =
      crucible::deserialize_region(crucible::effects::Alloc{}, bytes, arena);
  return raw == nullptr ? 0 : 1;
}
