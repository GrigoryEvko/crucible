// A Strict has no implicit conversion to its integer.  Only raw() gives the
// value, so a strict sum does not leave the family with no call.

#include <foundation/core/Arithmetic.h>

#include <cstdint>

int main() {
    ::foundation::core::Strict<std::uint64_t> const total{std::uint64_t{7}};
    std::uint64_t const bytes = total;
    return static_cast<int>(bytes);
}
