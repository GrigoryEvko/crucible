// checked_add takes two quantities of one unit: a sum of bytes and elements
// does not compile.

#include <foundation/core/Quantity.h>

#include <cstdint>

int main() {
    namespace core = ::foundation::core;
    core::Quantity<core::unit::Byte, std::uint64_t> const bytes{3};
    core::Quantity<core::unit::Element, std::uint64_t> const elements{4};
    return core::checked_add(bytes, elements).is_some() ? 0 : 1;
}
