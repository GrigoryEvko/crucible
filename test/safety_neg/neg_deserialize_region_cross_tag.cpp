// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for #1017 WRAP-Serialize-8
// (deserialize_region raw RegionNode* -> Tagged<RegionNode*, source::Loaded>).
//
// Premise: a RegionNode* reconstructed from validated serialized bytes
// is source::Loaded, not source::External.  Keeping those provenance
// lanes distinct prevents reload paths from being confused with raw
// FFI / disk-byte ingress paths.
//
// Distinct mismatch class from neg_deserialize_region_raw_region_ptr.cpp:
//   * Companion: implicit raw extraction rejected.
//   * This fixture: cross-tag assignment rejected.

#include <crucible/ir001/Serialize.h>
#include <crucible/safety/Tagged.h>

int main() {
  crucible::Arena arena{1024};
  std::span<const std::uint8_t> bytes{};

  using ExternalRegion = crucible::safety::Tagged<
      crucible::RegionNode*,
      crucible::safety::source::External>;

  // MUST fail: source::Loaded is not source::External.
  ExternalRegion wrong =
      crucible::deserialize_region(crucible::effects::Alloc{}, bytes, arena);
  return wrong.value() == nullptr ? 0 : 1;
}
