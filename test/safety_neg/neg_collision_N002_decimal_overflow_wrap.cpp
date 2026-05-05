// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-013 / N002: exact decimal types do not have modular wrap
// overflow semantics.

#include <crucible/safety/Fn.h>

#include <type_traits>

namespace fn = crucible::safety::fn;

namespace neg_collision_n002 {
struct Decimal64 {
    long long mantissa = 0;
    int exponent = 0;
};

using Bad = fn::Fn<Decimal64, fn::pred::True, fn::UsageMode::Linear,
                   crucible::effects::Row<>, fn::SecLevel::Public,
                   fn::proto::None, fn::lifetime::Static,
                   fn::source::FromInternal, fn::trust::Verified,
                   fn::ReprKind::Opaque, fn::cost::Unstated,
                   fn::precision::Exact, fn::space::Zero,
                   fn::OverflowMode::Wrap>;
}

namespace crucible::safety::fn::collision {
template <>
struct is_exact_decimal<::neg_collision_n002::Decimal64> : std::true_type {};
}

[[maybe_unused]] neg_collision_n002::Bad bad{};

int main() { return 0; }
