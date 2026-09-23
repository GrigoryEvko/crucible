// F105: constant time x an FP mode that names PreserveSubnormals.
//
// The second mismatch class: the mode states the slow setting explicitly
// rather than by omission.  The mode names the input flush, so F104
// stands down, and the pack trips F105 alone.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::constant_time,
                                ::fixy::atom::fp::mode<::fixy::atom::fp::FpFtz::PreserveSubnormals,
                                                       ::fixy::atom::fp::FpDenormalInput::DenormalsAreZero>> refused{};
    return 0;
}
