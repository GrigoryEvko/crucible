// A merge kind states why its results depend on the schedule.  The kind mix
// gives its two inputs in the order in which they come, and it opens the
// merge door with an empty reason.  allow_merge is consteval, and an empty
// reason makes its call no constant expression, so the signature does not
// compile.

#include <foundation/algebra/net/Net.h>

namespace {

using foundation::algebra::net::allow_merge;
using foundation::algebra::net::give;
using foundation::algebra::net::NetSignature;
using foundation::algebra::net::take;
using foundation::algebra::net::TypeId;

inline constexpr NetSignature<1, 1> mix_kinds{
    .type_names = {"Value"},
    .kinds = {{.name = "mix",
               .port_names = {"first", "second", "out"},
               .port_types = {take(TypeId{0}), take(TypeId{0}), give(TypeId{0})},
               .merge = allow_merge("")}}};

}  // namespace

int main() { return mix_kinds.kinds[0].merge.is_open() ? 0 : 1; }
