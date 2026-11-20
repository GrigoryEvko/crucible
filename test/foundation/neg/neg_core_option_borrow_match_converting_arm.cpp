// The borrow match of an Option gives the payload to its arm as a const
// reference.  An arm that takes a different class would read the payload
// through an implicit constructor of that class.  Here the arm takes Feet,
// and the payload is Meters.  The borrow match takes only an arm whose
// parameter is the payload type or a const reference to it, so it refuses
// this arm.

#include <foundation/core/Choice.h>

struct Meters {
    double value = 0.0;
};

struct Feet {
    double value = 0.0;
    constexpr Feet() noexcept = default;
    constexpr Feet(Meters meters) noexcept : value{meters.value * 3.28084} {}
};

int main() {
    ::foundation::core::Option<Meters> const length = ::foundation::core::Option<Meters>::some(Meters{1.0});
    double const seen = length.match([](Feet feet) noexcept { return feet.value; }, []() noexcept { return 0.0; });
    return seen > 3.0 ? 0 : 1;
}
