// The carrier admits any arithmetic type, because holding a float
// beside a flag is meaningful.  The three checked operations do not:
// the clamp they report is the end of an integer range, and a floating
// type saturates to an infinity the builtins never signal.

#include <fixy/Saturated.h>

int main() {
    const fixy::Saturated<float> sum = fixy::add_sat_checked(1.0f, 2.0f);
    return sum.was_clamped() ? 1 : 0;
}
