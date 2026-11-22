// Negative compile test.  This file must fail to compile.
//
// Fixture 2 of 2 for the loaded-region door of deserialize_region.
//
// Premise: a region that the loader rebuilt from Cipher bytes is
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
    ::fixy::BumpArena arena{1024};
    ::fixy::View<const std::byte> bytes{};

    // The payload is the payload of a loaded region, so only the tag differs.
    using ExternalRegion = ::fixy::Tagged<::fixy::Ref<crucible::RegionNode>, ::fixy::tags::source::External>;

    // Must fail: source::Loaded is not source::External.
    ExternalRegion wrong = crucible::deserialize_region(::foundation::effects::testing::test().alloc, bytes, arena)
                               .expect("the fixture names the type of a loaded region");
    return wrong.value()->content_hash ? 0 : 1;
}
