// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a FpFtzPinned<FlushToZero, int> where a
// FpRoundingPinned<RoundToNearestEven, int> parameter is declared —
// DIFFERENT FP sub-axes instantiate FpModePinned with NTTPs of
// DIFFERENT enum types (FpFtz vs FpRounding), so the resulting class
// types are completely distinct and conversion is rejected.
//
// HS14 substrate-side rejection gate per CLAUDE.md §XVI: V-090's
// per-axis row_hash salts (0x21..0x2B) are load-bearing precisely
// because every per-axis wrapper carries its sub-axis at the type
// level.  If FpFtzPinned could silently convert to FpRoundingPinned,
// the federation cache would happily route an FTZ-tagged value into
// the rounding-tagged slot — Lemma 7(b)'s cross-axis numerics
// disjointness collapses.  This fixture pins that the type system
// catches the cross-axis confusion at compile time.
//
// Concrete bug-class this catches: a contributor adds a generic
// "FpModePinned<auto, T>"-to-"FpModePinned<auto, T>" conversion
// pathway (e.g., via a too-permissive forwarding constructor), and
// suddenly per-axis wrappers become silently interchangeable.
//
// Pairs with neg_fp_mode_cross_mode_assign.cpp for the 2-fixture
// HS14 floor — one fixture per distinct mismatch class:
//   1. cross_mode_assign: same axis, different mode → assign reject.
//   2. cross_axis_pass:   different axis → function-arg type reject (this).
//
// Substring "cannot convert" / "no match" / "could not match"
// pins the diagnostic family.

#include <crucible/safety/FpMode.h>

namespace {

// Function expecting a SPECIFIC sub-axis (FpRounding).
[[maybe_unused]] void requires_fp_rounding(
    crucible::safety::FpRoundingPinned<
        crucible::safety::FpRounding::RoundToNearestEven, int> const&) {}

}  // namespace

int main() {
    using namespace crucible::safety;

    FpFtzPinned<FpFtz::FlushToZero, int> ftz{42};

    // Should FAIL: cross-axis pass — FpFtzPinned IS NOT a
    // FpRoundingPinned, despite both being FpModePinned instantiations
    // (the NTTPs are of different enum types so the class types are
    // completely disjoint).
    requires_fp_rounding(ftz);

    return 0;
}
