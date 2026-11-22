// Negative-compile test.  This file must fail to compile.
//
// Mismatch class 1 of 2 for BackgroundThread::DeviceCapability: the
// compiler rejects an assignment of a raw uint64_t (provenance bypass).
//
// The type is fixy::Tagged<uint64_t, tags::source::Meridian>, and the
// private field of BackgroundThread holds it.  The tag says that the startup calibration
// pass measured the value on real silicon.  The constructor from the value
// is explicit and private, so mint_tagged<Meridian> is the one door.  If
// this fixture compiles, any integer can take the place of a measured
// hardware identity, and recipe selection can pick a kernel for hardware
// that is not there.
//
// Companion: neg_device_capability_cross_source_assignment.cpp refuses a
// value under a different source tag (provenance laundering).

#include <crucible/BackgroundThread.h>

#include <cstdint>

static void assign_raw_capability(crucible::BackgroundThread::DeviceCapability& capability, std::uint64_t raw_cap) {
    // The compiler must reject this line: no assignment takes a raw uint64_t.
    capability = raw_cap;
}

int main() {
    (void)&assign_raw_capability;
    return 0;
}
