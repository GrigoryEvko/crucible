// *_sat_from returns Saturated<T> so the clamped bit is carried out of
// memory-resident counter arithmetic.  It must not implicitly decay to
// raw T.

#include <fixy/Saturate.h>

int main() {
    unsigned counter = ~0u;
    unsigned wrong = fixy::sat::add_sat_from(counter, 1u);
    return static_cast<int>(wrong);
}
