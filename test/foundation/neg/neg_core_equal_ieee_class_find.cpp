// Negative-compile test. This file must fail to compile.
//
// The defaulted == of Rate compares its double by IEEE 754: +0 equals -0, and
// a NaN equals nothing, also itself.  So find over a View of rates could miss
// a NaN that the view holds.  Rate is no Equatable type, and find refuses it.
//
// Expected diagnostic: no find over a View of Rate, because Rate is not
// Equatable.

#include <foundation/core/Algorithms.h>
#include <foundation/core/Array.h>

namespace {
struct Rate {
    double per_second = 0.0;
    friend constexpr bool operator==(Rate const&, Rate const&) noexcept = default;
};
}  // namespace

int main() {
    ::foundation::core::FixedArray<Rate, 2> const rates{};
    return ::foundation::core::find(rates.view(), Rate{}).is_some() ? 0 : 1;
}
