// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-010 / M012: concurrent monotonic mutation needs atomic repr.

#include <crucible/safety/Fn.h>

namespace fn = crucible::safety::fn;

namespace neg_collision_m012 {
using Bad = fn::Fn<int, fn::pred::True, fn::UsageMode::Linear,
                   crucible::effects::Row<crucible::effects::Effect::Bg>,
                   fn::SecLevel::Public, fn::proto::None, fn::lifetime::Static,
                   fn::source::FromInternal, fn::trust::Verified,
                   fn::ReprKind::Opaque, fn::cost::Unstated,
                   fn::precision::Exact, fn::space::Zero,
                   fn::OverflowMode::Trap, fn::MutationMode::Monotonic>;
}

[[maybe_unused]] neg_collision_m012::Bad bad{};

int main() { return 0; }
