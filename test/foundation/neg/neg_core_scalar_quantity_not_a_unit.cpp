// A unit of a Quantity is a tag of namespace unit: a class with a dimension
// and a ratio of its own is no unit, so the Quantity does not compile.

#include <foundation/core/Quantity.h>

#include <cstdint>

struct Kibibyte {
    using dimension = ::foundation::core::unit::Memory;
    static constexpr std::uint64_t ratio = 1024;
};

int main() { return ::foundation::core::Quantity<Kibibyte, std::uint64_t>{2}.raw() == 2 ? 0 : 1; }
