// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-008 / E044: constant-time code cannot include async scheduling.

#include <crucible/safety/Fn.h>

#include <type_traits>

namespace fn = crucible::safety::fn;

namespace neg_collision_e044 {
using Bad = fn::Fn<int, fn::pred::True, fn::UsageMode::Linear,
                   crucible::effects::Row<>, fn::SecLevel::Public>;
}

namespace crucible::safety::fn::collision {
template <>
struct marks_async<::neg_collision_e044::Bad> : std::true_type {};
template <>
struct marks_ct<::neg_collision_e044::Bad> : std::true_type {};
}

[[maybe_unused]] neg_collision_e044::Bad bad{};

int main() { return 0; }
