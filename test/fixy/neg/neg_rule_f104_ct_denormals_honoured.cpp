// F104: constant time x an FP mode that names HonorDenormals.
//
// The second mismatch class: the mode states the slow setting explicitly
// rather than by omission.  The mode names the output flush, so F105
// stands down, and the pack trips F104 alone.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::constant_time,
                                ::fixy::atom::fp::mode<::fixy::atom::fp::FpDenormalInput::HonorDenormals,
                                                       ::fixy::atom::fp::FpFtz::FlushToZero>> refused{};
    return 0;
}
