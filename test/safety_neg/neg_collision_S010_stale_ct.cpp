// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-016 / S010: runtime staleness checks defeat CT timing.

#include <crucible/safety/Fn.h>

#include <type_traits>

namespace fn = crucible::safety::fn;

namespace neg_collision_s010 {
using Bad = fn::Fn<int, fn::pred::True, fn::UsageMode::Linear,
                   crucible::effects::Row<>, fn::SecLevel::Public,
                   fn::proto::None, fn::lifetime::Static,
                   fn::source::FromInternal, fn::trust::Verified,
                   fn::ReprKind::Opaque, fn::cost::Unstated,
                   fn::precision::Exact, fn::space::Zero,
                   fn::OverflowMode::Trap, fn::MutationMode::Immutable,
                   fn::ReentrancyMode::NonReentrant, fn::size_pol::Unstated,
                   1, fn::stale::Stale<5>>;
}

namespace crucible::safety::fn::collision {
template <>
struct marks_ct<::neg_collision_s010::Bad> : std::true_type {};
}

[[maybe_unused]] neg_collision_s010::Bad bad{};

int main() { return 0; }
