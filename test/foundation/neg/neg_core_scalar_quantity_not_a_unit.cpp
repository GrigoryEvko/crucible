// A unit of a Quantity is a unit of namespace unit: a class with a dimension
// and a scale of its own is no unit, so the Quantity does not compile.

#include <foundation/core/Quantity.h>

#include <cstdint>

struct Kilobyte {
    static constexpr ::foundation::core::unit::Dimension dimension{.memory = 1};
    static constexpr ::foundation::core::unit::Scale scale{.power = 1};
};

int main() { return ::foundation::core::Quantity<Kilobyte, std::uint64_t>{2}.raw() == 2 ? 0 : 1; }
