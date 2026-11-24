// The range of a sum is the sum of the ranges, and a range past
// std::int64_t does not compile, so no sum of two Ranged values overflows.

#include <foundation/core/Index.h>

#include <cstdint>

int main() {
    using Top = ::foundation::core::Ranged<0, ::foundation::core::max_of<std::int64_t>>;
    using Bit = ::foundation::core::Ranged<0, 1>;
    auto const sum = Top::constant<1>() + Bit::constant<1>();
    return static_cast<int>(sum.raw());
}
