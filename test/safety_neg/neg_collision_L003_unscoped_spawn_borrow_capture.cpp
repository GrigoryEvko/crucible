// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-014 / L003: an unscoped spawn cannot capture a borrow.

#include <crucible/safety/Fn.h>

#include <type_traits>

namespace fn = crucible::safety::fn;

namespace neg_collision_l003 {
using Bad = fn::Fn<int, fn::pred::True, fn::UsageMode::Borrow,
                   crucible::effects::Row<>, fn::SecLevel::Public>;
}

namespace crucible::safety::fn::collision {
template <>
struct marks_unscoped_spawn<::neg_collision_l003::Bad> : std::true_type {};
}

[[maybe_unused]] neg_collision_l003::Bad bad{};

int main() { return 0; }
