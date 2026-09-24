// F101: a replay-deterministic payload x FP reassociation permitted.
//
// Reassociation reorders the sum, and floating-point addition is not
// associative, so a replayed run produces different bits and the
// payload's DetSafe claim fails.  The old predicate refuses both the
// unrestricted rewrite and the bounded tree depth: a log-N tree pins the
// topology but not the order in which the compiler chooses to fill it.
//
// The mode atom is a product; this pack names one setting and leaves the
// other three at their strict values, which is what keeps the fixture on
// F101 and off F102.

#include <fixy/Bands.h>
#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, int>,
                                ::fixy::atom::fp::mode<::fixy::atom::fp::FpReassociate::UnrestrictedRewrite>>
        refused{};
    return 0;
}
