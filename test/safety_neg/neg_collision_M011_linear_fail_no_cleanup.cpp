// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-015 / M011: linear resources live across Fail need cleanup.

#include <crucible/safety/Fn.h>

#include <type_traits>

namespace fn = crucible::safety::fn;

namespace neg_collision_m011 {
using Bad = fn::Fn<int, fn::pred::True, fn::UsageMode::Linear,
                   crucible::effects::Row<>, fn::SecLevel::Public>;
}

namespace crucible::safety::fn::collision {
template <>
struct marks_fail<::neg_collision_m011::Bad> : std::true_type {};
template <>
struct marks_linear_uncleaned_fail<::neg_collision_m011::Bad> : std::true_type {};
}

[[maybe_unused]] neg_collision_m011::Bad bad{};

int main() { return 0; }
