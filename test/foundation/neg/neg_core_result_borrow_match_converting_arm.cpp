// The borrow match of a Result gives the value to its arm as a const
// reference.  An arm that takes a different class would read the value
// through an implicit constructor of that class.  Here the arm takes Feet,
// and the value is Meters.  The borrow match takes only an arm whose
// parameter is the part type or a const reference to it, so it refuses this
// arm.

#include <foundation/core/Choice.h>

enum class Refusal : unsigned char {
    none
};

struct Meters {
    double value = 0.0;
};

struct Feet {
    double value = 0.0;
    constexpr Feet() noexcept = default;
    constexpr Feet(Meters meters) noexcept : value{meters.value * 3.28084} {}
};

int main() {
    ::foundation::core::Result<Meters, Refusal> const length{Meters{2.0}};
    double const seen =
        length.match([](Feet feet) noexcept { return feet.value; }, [](Refusal) noexcept { return 0.0; });
    return seen > 6.0 ? 0 : 1;
}
