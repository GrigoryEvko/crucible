// A Saturated value is read-only after construction.  A write through
// value() keeps the flag of the clamp on a value that no clamp gave, so
// the accessor gives a const reference and the write does not compile.

#include <fixy/Saturated.h>

#include <cstdint>

int main() {
    fixy::Saturated<std::uint8_t> sum = fixy::add_sat_checked<std::uint8_t>(200, 100);
    sum.value() = 7;
    return sum.was_clamped() ? 0 : 1;
}
