// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Mismatch class 2 of 2 for BackgroundThread::device_capability: the
// compiler rejects an assignment of a value under tags::source::Calibrated,
// because the field holds tags::source::Meridian (provenance laundering).
//
// Calibrated marks the result of a runtime calibration probe.  Meridian
// marks a value that the startup calibration pass measured on real
// silicon.  The two tags wrap the same uint64_t, but they are different
// types, and the retag catalog has no edge from Calibrated to Meridian.
// If this fixture compiles, a value that a probe measured at run time can
// take the place of the startup measurement.
//
// Companion: neg_device_capability_raw_assignment.cpp refuses a raw
// uint64_t (provenance bypass).

#include <crucible/BackgroundThread.h>

#include <cstdint>

static void assign_calibrated_capability(crucible::BackgroundThread& background) {
    auto calibrated = ::fixy::mint_tagged<::fixy::tags::source::Calibrated>(std::uint64_t{90});

    // The compiler must reject this line: the two sources are different types.
    background.device_capability = calibrated;
}

int main() {
    (void)&assign_calibrated_capability;
    return 0;
}
