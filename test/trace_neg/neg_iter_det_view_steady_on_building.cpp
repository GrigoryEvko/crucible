// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: minting a Steady view of an IterationDetector that is still
// Building (signature_len.get() < K) during constant evaluation, which
// fires the precondition view_ok(...) of ::fixy::mint_view.
//
// iter_det_state::Steady requires signature_len.get() == K (= 5).  A fresh
// detector has signature_len.get() == 0, so the Steady view_ok overload
// returns false and the precondition fails.  In a constant evaluation the
// failed precondition makes the expression non-constant.
//
// Companion fixture to neg_iter_det_view_in_field.cpp:
//   - This one is the value-level check at mint time.  It catches a
//     view_ok overload that answers true for the wrong phase, or a caller
//     that mints a view before the detector left Building.
//   - That one is the structural check that refuses a view stored as a
//     member.

#include <crucible/IterationDetectorState.h>
#include <fixy/ScopedView.h>

// Returning bool rather than the view keeps the diagnostic on the
// precondition, and not on a view that would outlive its carrier.
constexpr bool mint_steady_on_fresh_detector() {
    crucible::IterationDetector detector{};
    auto view = ::fixy::mint_view<crucible::iter_det_state::Steady>(detector);
    (void)view;
    return true;
}

int main() {
    constexpr bool unused = mint_steady_on_fresh_detector();
    (void)unused;
    return 0;
}
