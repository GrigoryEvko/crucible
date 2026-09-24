// F103: constant time x FP reassociation by an unrestricted rewrite.
//
// The compiler can pick the tree of the sum from the magnitudes of the
// operands, so the count of operations follows the data.  The mode names
// both flushes, so F104 and F105 stand down, and the pack trips F103
// alone.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::constant_time,
                                ::fixy::atom::fp::mode<::fixy::atom::fp::FpReassociate::UnrestrictedRewrite,
                                                       ::fixy::atom::fp::FpDenormalInput::DenormalsAreZero,
                                                       ::fixy::atom::fp::FpFtz::FlushToZero>> refused{};
    return 0;
}
