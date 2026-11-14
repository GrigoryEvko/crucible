// A checked operation returns Saturated<T>, so the clamped bit leaves the
// arithmetic with the value.  The result must not decay to a raw T.

#include <fixy/Saturated.h>

int main() {
    unsigned counter = ~0u;
    unsigned wrong = fixy::add_sat_checked(counter, 1u);
    return static_cast<int>(wrong);
}
