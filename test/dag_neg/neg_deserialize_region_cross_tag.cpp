// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Fixture 2 of 2 for the loaded-region door of deserialize_region.
//
// Premise: a RegionNode* that the loader rebuilt from Cipher bytes is
// source::Loaded, not source::External.  Loaded is earned on the
// Replayed to Loaded edge, and External is raw input.  Keeping the two
// lanes apart prevents a reload path from being confused with a raw
// FFI or disk ingress path.
//
// Distinct mismatch class from neg_deserialize_region_raw_region_ptr.cpp:
//   * Companion: the result converts to no raw pointer.
//   * This fixture: a Loaded region does not pass as an External one.

#include <crucible/Serialize.h>
#include <fixy/Tagged.h>

int main() {
    crucible::Arena arena{1024};
    std::span<const std::uint8_t> bytes{};

    using ExternalRegion = ::fixy::Tagged<crucible::RegionNode*, ::fixy::tags::source::External>;

    // MUST fail: source::Loaded is not source::External.
    ExternalRegion wrong = *crucible::deserialize_region(::foundation::effects::testing::test().alloc, bytes, arena);
    return wrong.value() == nullptr ? 0 : 1;
}
