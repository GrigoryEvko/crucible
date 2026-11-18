// Two quantities compare only in one unit: a compare of bytes per second with
// flops per second does not compile.

#include <foundation/core/Quantity.h>

#include <cstdint>

int main() {
    namespace core = ::foundation::core;
    core::Quantity<core::unit::BytePerSecond, std::uint64_t> const bandwidth{8};
    core::Quantity<core::unit::FlopPerSecond, std::uint64_t> const peak{8};
    return bandwidth == peak ? 0 : 1;
}
