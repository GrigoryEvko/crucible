// Negative-compile test. This file must fail to compile.
//
// Rate states the mark equal_parts, but its defaulted == compares its double
// by IEEE 754: +0 equals -0, and a NaN equals nothing, also itself.  The walk
// of the float rule trusts the mark only on a class of the base, and Rate is
// in an unnamed namespace.  So Rate is no Equatable type, and an Option of
// Rate has no ==.
//
// Expected diagnostic: no == of two Options of Rate.

#include <foundation/core/Choice.h>

namespace {
struct[[= ::foundation::core::equal_parts{}]] Rate {
    double per_second = 0.0;
    friend constexpr bool operator==(Rate const&, Rate const&) noexcept = default;
};
}  // namespace

int main() {
    auto const left = ::foundation::core::Option<Rate>::some(Rate{});
    auto const right = ::foundation::core::Option<Rate>::some(Rate{});
    return left == right ? 0 : 1;
}
