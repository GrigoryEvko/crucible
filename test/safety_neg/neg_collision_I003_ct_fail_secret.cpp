// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-009 / I003: CT code cannot fail on a secret-dependent branch.

#include <crucible/safety/Fn.h>

#include <type_traits>

namespace fn = crucible::safety::fn;

namespace neg_collision_i003 {
using Bad = fn::Fn<int, fn::pred::True, fn::UsageMode::Linear,
                   crucible::effects::Row<>, fn::SecLevel::Public>;
}

namespace crucible::safety::fn::collision {
template <>
struct marks_ct<::neg_collision_i003::Bad> : std::true_type {};
template <>
struct marks_fail<::neg_collision_i003::Bad> : std::true_type {};
template <>
struct marks_fail_on_secret<::neg_collision_i003::Bad> : std::true_type {};
}

[[maybe_unused]] neg_collision_i003::Bad bad{};

int main() { return 0; }
