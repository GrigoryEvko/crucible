// A product has the product of the units: bytes per second times seconds are
// bytes, and a sum of these bytes and seconds does not compile.

#include <foundation/core/Quantity.h>

#include <cstdint>

int main() {
    namespace core = ::foundation::core;
    core::Quantity<core::unit::BytePerSecond, std::uint64_t> const rate{3};
    core::Quantity<core::unit::Second, std::uint64_t> const seconds{4};
    auto const bytes = core::checked_mul(rate, seconds).expect("3 times 4 fits");
    return core::checked_add(bytes, seconds).is_some() ? 0 : 1;
}
