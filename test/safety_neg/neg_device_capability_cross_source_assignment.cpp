// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-BgThread-4 #875 (Tagged half), mismatch class #2 of 2:
// `Tagged<uint64_t, source::Calibrated>` CANNOT BE ASSIGNED TO A
// `Tagged<uint64_t, source::Meridian>` FIELD WITHOUT EXPLICIT RETAG.
//
// Companion to neg_device_capability_raw_assignment.cpp.  The raw-
// uint64_t fixture catches a caller bypassing the provenance gate
// entirely.  THIS fixture catches the SUBTLER defect mode:
// provenance LAUNDERING via cross-source mixing.  A caller has a
// `Tagged<uint64_t, source::Calibrated>` (e.g. a result from a
// general-purpose runtime calibration probe — not the Meridian
// startup pass), and tries to assign it to the `Tagged<uint64_t,
// source::Meridian>` field.  Both wrap `uint64_t`, but the Tag
// distinguishes them as nominally distinct types and the type system
// refuses the swap.
//
// Without this gate, a runtime-calibrated value would silently take
// residence in device_capability and the source::Meridian invariant
// ("measured from real silicon at startup, not synthesized or
// recalibrated mid-runtime") would be subverted — Forge phase
// E.RecipeSelect would receive an off-spec hardware-identity value
// and pick an incompatible kernel.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.  Mirror of the
// WRAP-CCtx-2 #904 active_region_ cross-source fixture (same Tagged
// shape, different source-tag axis: source::Arena vs source::Vigil →
// here, source::Calibrated vs source::Meridian).

#include <crucible/safety/Tagged.h>
#include <cstdint>

int main() {
    using MeridianCap   = ::crucible::safety::Tagged<
        std::uint64_t, ::crucible::safety::source::Meridian>;
    using CalibratedCap = ::crucible::safety::Tagged<
        std::uint64_t, ::crucible::safety::source::Calibrated>;

    CalibratedCap calibrated_tagged{90};

    // Should FAIL: Tagged<T, source::Calibrated> and Tagged<T,
    // source::Meridian> are DISTINCT nominal types despite identical
    // value_type T.  The Tag is the type-level provenance witness;
    // cross-source assignment requires explicit `Tagged<T, NewTag>{old.value()}`
    // re-wrapping (provenance is re-asserted at the call site, not
    // auto-laundered).
    MeridianCap field = calibrated_tagged;
    (void)field;
    return 0;
}
