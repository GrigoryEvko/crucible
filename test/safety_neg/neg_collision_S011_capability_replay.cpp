// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-017 / S011: ephemeral capabilities cannot be replay-stable
// inputs unless a content-addressed reconstruction path is declared.

#include <crucible/safety/Fn.h>

#include <type_traits>

namespace fn = crucible::safety::fn;

namespace neg_collision_s011 {
using Bad = fn::Fn<int, fn::pred::True, fn::UsageMode::Capability,
                   crucible::effects::Row<>, fn::SecLevel::Public>;
}

namespace crucible::safety::fn::collision {
template <>
struct marks_replay_required<::neg_collision_s011::Bad> : std::true_type {};
}

[[maybe_unused]] neg_collision_s011::Bad bad{};

int main() { return 0; }
