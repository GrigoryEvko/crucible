// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: minting a Steady-state ScopedView on an IterationDetector
// that is in Building state (signature_len.get() < K) at constant
// evaluation — fires mint_view's `pre(view_ok(...))` precondition.
//
// Per WRAP-IterDet-4 (#930), iter_det_state::Steady requires
// signature_len.get() == K (= 5) to mint.  A default-constructed
// detector has signature_len.get() == 0 < K, so the Steady view_ok
// overload returns false, mint_view's pre fires.  In constexpr
// context, P1494R5 makes the contract violation non-constant — using
// it where a constant is required is ill-formed (diagnostic says
// "is not a constant expression" / "contract" / "constexpr").
//
// Companion fixture to neg_iter_det_view_in_field.cpp:
//   - This one is the value-level boundary check at MINT TIME — pre
//     fires when the runtime predicate disagrees with the asserted tag.
//     Catches a future regression where view_ok's overload returns
//     true for the wrong state, or where a caller mints a view in the
//     wrong state assuming the detector has progressed past Building.
//   - That one is the structural Tier-2 lifetime check at FIELD AUDIT
//     time — no_scoped_view_field_check fires on a class that stores
//     a ScopedView<IterationDetector, ...> as a member.
//
// Together they pin BOTH the runtime mint-time gate AND the structural
// field-storage gate of WRAP-IterDet-4 — distinct mismatch classes
// per HS14's "≥ 2 fixtures per new soundness gate, each demonstrating
// a distinct mismatch class".

#include <crucible/IterationDetectorState.h>

// Wrap the failing call in a constexpr function so the pre fires
// during constant evaluation (P1494R5).  Returning bool rather than
// the view itself dodges the unrelated "ScopedView outlives carrier"
// diagnostic that would also appear if we returned the view directly.
constexpr bool mint_steady_on_fresh_detector() {
    crucible::IterationDetector detector{};
    // detector.signature_len.get() == 0 < K = 5.  iter_det_state::
    // Steady's view_ok overload returns
    //     detector.signature_len.get() == K
    // which is false at this point.  mint_view's
    //     pre(view_ok(detector, std::type_identity<Steady>{}))
    // fires; under consteval the result is non-constant.
    auto sv = crucible::safety::mint_view<
        crucible::iter_det_state::Steady>(detector);
    (void)sv;
    return true;
}

int main() {
    constexpr bool unused = mint_steady_on_fresh_detector();
    (void)unused;
    return 0;
}
