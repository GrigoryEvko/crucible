// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-011 / P002: ghost values are erased and cannot drive runtime code.

#include <crucible/safety/Fn.h>

#include <type_traits>

namespace fn = crucible::safety::fn;

namespace neg_collision_p002 {
using Bad = fn::Fn<int, fn::pred::True, fn::UsageMode::Ghost,
                   crucible::effects::Row<>, fn::SecLevel::Public>;
}

namespace crucible::safety::fn::collision {
template <>
struct marks_runtime_ghost_use<::neg_collision_p002::Bad> : std::true_type {};
}

[[maybe_unused]] neg_collision_p002::Bad bad{};

int main() { return 0; }
