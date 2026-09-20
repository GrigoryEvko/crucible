// *_sat_from returns Saturated<T> so the clamped bit is carried out of
// memory-resident counter arithmetic.  It must not implicitly decay to
// raw T.
//
// Old spelling: test/safety_neg/neg_saturate_from_raw_escape.cpp.

#include <fixy/Saturate.h>

int main() {
    unsigned counter = ~0u;
    unsigned wrong = fixy::sat::add_sat_from(counter, 1u);
    return static_cast<int>(wrong);
}
