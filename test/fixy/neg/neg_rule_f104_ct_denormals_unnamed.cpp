// F104: constant time x an FP mode that does not name the input flush.
//
// A setting the mode does not name is at its strict value, and for
// denormal inputs that value honours them.  A denormal input costs 30 to
// 100 times the cycles of a normal one.  The mode names the output flush,
// so F105 stands down, and the pack trips F104 alone.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::constant_time,
                                ::fixy::atom::fp::mode<::fixy::atom::fp::FpFtz::FlushToZero>> refused{};
    return 0;
}
