// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-007 / L002: a borrow capture cannot bridge async suspension.

#include <crucible/safety/Fn.h>

#include <type_traits>

namespace fn = crucible::safety::fn;

namespace neg_collision_l002 {
using Bad = fn::Fn<int, fn::pred::True, fn::UsageMode::Borrow,
                   crucible::effects::Row<>, fn::SecLevel::Public>;
}

namespace crucible::safety::fn::collision {
template <>
struct marks_async<::neg_collision_l002::Bad> : std::true_type {};
}

[[maybe_unused]] neg_collision_l002::Bad bad{};

int main() { return 0; }
