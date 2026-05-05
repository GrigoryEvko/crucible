// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-012 / I004: classified async session traffic needs CT or
// explicit declassification.

#include <crucible/safety/Fn.h>
#include <crucible/sessions/Session.h>

#include <type_traits>

namespace fn = crucible::safety::fn;
namespace proto = crucible::safety::proto;

namespace neg_collision_i004 {
using Bad = fn::Fn<int, fn::pred::True, fn::UsageMode::Linear,
                   crucible::effects::Row<>, fn::SecLevel::Classified,
                   proto::Send<int, proto::End>>;
}

namespace crucible::safety::fn::collision {
template <>
struct marks_async<::neg_collision_i004::Bad> : std::true_type {};
}

[[maybe_unused]] neg_collision_i004::Bad bad{};

int main() { return 0; }
