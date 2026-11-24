// A Strict takes a value of its own type.  A std::uint32_t converts to a
// std::uint64_t with no warning, so the deleted constructor refuses it, and a
// caller widens the value first.

#include <foundation/core/Arithmetic.h>

#include <cstdint>

int main() {
    ::foundation::core::Strict<std::uint64_t> const total{std::uint32_t{7}};
    return static_cast<int>(total.raw());
}
