// F105: constant time x an FP mode that does not name the output flush.
//
// A setting the mode does not name is at its strict value, and for
// subnormal results that value preserves them.  A subnormal result costs
// 30 to 100 times the cycles of a normal one.  The mode names the input
// flush, so F104 stands down, and the pack trips F105 alone.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::constant_time,
                                ::fixy::atom::fp::mode<::fixy::atom::fp::FpDenormalInput::DenormalsAreZero>> refused{};
    return 0;
}
