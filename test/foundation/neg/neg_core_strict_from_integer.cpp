// A Strict has no implicit conversion from its integer.  The constructor is
// explicit, so a plain integer does not become a Strict with no call.

#include <foundation/core/Arithmetic.h>

#include <cstdint>

int main() {
    ::foundation::core::Strict<std::uint64_t> const total = std::uint64_t{7};
    return static_cast<int>(total.raw());
}
