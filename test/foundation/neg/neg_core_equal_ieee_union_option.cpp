// Negative-compile test. This file must fail to compile.
//
// Rate holds its double in a member of a union, and its own == compares the
// double by IEEE 754, written as two orders: +0 equals -0, and a NaN equals
// nothing, also itself.
// The walk of the float rule reads each member of a union part, so Rate is
// no Equatable type, and an Option of Rate has no ==.  The fixture
// neg_core_equal_ieee_class_option holds the same double as a plain member.
//
// Expected diagnostic: no == of two Options of Rate.

#include <foundation/core/Choice.h>

namespace {
struct Rate {
    union Cell {
        double per_second;
        int code;
    };
    Cell cell{.per_second = 0.0};
    friend constexpr bool operator==(Rate const& left, Rate const& right) noexcept {
        return left.cell.per_second <= right.cell.per_second && right.cell.per_second <= left.cell.per_second;
    }
};
}  // namespace

int main() {
    auto const left = ::foundation::core::Option<Rate>::some(Rate{});
    auto const right = ::foundation::core::Option<Rate>::some(Rate{});
    return left == right ? 0 : 1;
}
