// S010: constant time x the narrowest staleness window.
//
// A stale value makes the body check freshness at run time, and that
// check is a branch whose time follows the data.  No window is narrow
// enough, so a window of one is refused.  A constant-time binding is
// classified, so the corpus entry staleness_secret_without_declassify
// refuses the same pack, and the message names both.  S010 is the only
// collision rule it trips.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::constant_time, ::fixy::atom::stale_to<1>> refused{};
    return 0;
}
