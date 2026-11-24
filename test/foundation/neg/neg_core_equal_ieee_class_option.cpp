// Negative-compile test. This file must fail to compile.
//
// An Option compares its payloads with equal, which asks for an Equatable
// payload.  The defaulted == of Rate compares its double by IEEE 754, so an
// Option of Rate has no ==.
//
// Expected diagnostic: no == of two Options of Rate.

#include <foundation/core/Choice.h>

namespace {
struct Rate {
    double per_second = 0.0;
    friend constexpr bool operator==(Rate const&, Rate const&) noexcept = default;
};
}  // namespace

int main() {
    auto const left = ::foundation::core::Option<Rate>::some(Rate{});
    auto const right = ::foundation::core::Option<Rate>::some(Rate{});
    return left == right ? 0 : 1;
}
