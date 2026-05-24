// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-BgThread-4 #875 (Tagged half), mismatch class #1 of 2:
// RAW uint64_t CANNOT BE ASSIGNED DIRECTLY TO A
// `Tagged<uint64_t, source::Meridian>` FIELD.
//
// `Tagged<T, S>` requires explicit construction via `Tagged<T, S>{value}`
// — the `explicit` ctor refuses implicit conversion from the raw `T`
// value.  This catches the production-side defect mode where a caller
// does `device_capability = some_uint64;` (raw assignment) when the
// migration intent was `device_capability = DeviceCapability{some_uint64};`
// (typed construction).  Without this gate, an arbitrary uint64_t —
// not necessarily the result of the Meridian startup calibration pass
// — could leak into BackgroundThread's device_capability field,
// breaking the source::Meridian provenance invariant ("this value
// was measured from real silicon at startup, not synthesized or
// defaulted to 0 mid-runtime").
//
// Companion fixture: neg_device_capability_cross_source_assignment.cpp
//   * That one catches cross-source mixing (Tagged<...,source::Calibrated>
//     → Tagged<...,source::Meridian>) — provenance LAUNDERING.
//   * This one catches raw-uint64_t admission — provenance BYPASS.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.  Mirror of the
// WRAP-CCtx-2 #904 active_region_ raw-assignment fixture (same Tagged
// shape, different source tag / wrapped type).

#include <crucible/safety/Tagged.h>
#include <cstdint>

int main() {
    using DeviceCapability = ::crucible::safety::Tagged<
        std::uint64_t, ::crucible::safety::source::Meridian>;

    std::uint64_t raw_cap = 90;  // sm_90 encoded

    // Should FAIL: implicit conversion from raw uint64_t to
    // Tagged<uint64_t, source::Meridian> is rejected by the explicit
    // ctor.  Migration intent is `DeviceCapability{raw_cap}` — never
    // `= raw_cap`.
    DeviceCapability field = raw_cap;
    (void)field;
    return 0;
}
