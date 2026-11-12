// convert takes two units of one dimension: seconds do not convert to bytes.

#include <foundation/core/Quantity.h>

#include <cstdint>

int main() {
    namespace core = ::foundation::core;
    core::Quantity<core::unit::Second, std::int64_t> const seconds{3};
    return core::convert<core::unit::Byte>(seconds).is_some() ? 0 : 1;
}
