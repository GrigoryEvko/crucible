// Two Strict values of different types do not mix: the sum of a
// Strict<std::uint32_t> and a Strict<std::uint64_t> does not compile, so no
// promotion hides a conversion.

#include <foundation/core/Arithmetic.h>

#include <cstdint>

int main() {
    ::foundation::core::Strict<std::uint32_t> const count{std::uint32_t{1}};
    ::foundation::core::Strict<std::uint64_t> const total{std::uint64_t{2}};
    return static_cast<int>((count + total).raw());
}
