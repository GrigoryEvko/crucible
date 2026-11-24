// A Strict does not mix with a plain integer: the sum of a Strict and an
// integer of its type does not compile, so each operand of a strict sum is
// a Strict.

#include <foundation/core/Arithmetic.h>

#include <cstdint>

int main() {
    ::foundation::core::Strict<std::uint64_t> const total{std::uint64_t{7}};
    return static_cast<int>((total + std::uint64_t{1}).raw());
}
