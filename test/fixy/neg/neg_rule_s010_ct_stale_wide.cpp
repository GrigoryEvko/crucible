// S010: constant time x a wide staleness window.
//
// The second mismatch class: a window wide enough that the freshness
// check is the common path rather than the exception.  The corpus entry
// staleness_secret_without_declassify refuses the same pack, and S010 is
// the only collision rule it trips.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::constant_time, ::fixy::atom::stale_to<1000000>> refused{};
    return 0;
}
