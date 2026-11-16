// Each agent of a dataflow waits on its inputs.  Two adds that take each
// other's sum never start, although no vicious circle stands in the net at
// the start, so check_dataflow refuses the net and names the cycle.

#include <foundation/algebra/net/Net.h>

namespace {

using foundation::algebra::net::AgentId;
using foundation::algebra::net::give;
using foundation::algebra::net::KindId;
using foundation::algebra::net::Net;
using foundation::algebra::net::NetVerdict;
using foundation::algebra::net::PortRef;
using foundation::algebra::net::take;
using foundation::algebra::net::TypeId;
using foundation::algebra::net::Wire;

inline constexpr Net<1, 2, 4, 4> crossed_adds{
    .signature = {.type_names = {"Value"},
                  .kinds = {{.name = "num", .port_names = {"value"}, .port_types = {give(TypeId{0})}},
                            {.name = "add",
                             .port_names = {"left", "right", "sum"},
                             .port_types = {take(TypeId{0}), take(TypeId{0}), give(TypeId{0})}}}},
    .agents = {KindId{0}, KindId{0}, KindId{1}, KindId{1}},
    .wires = {
        Wire{PortRef{AgentId{0}, 0}, PortRef{AgentId{2}, 0}}, Wire{PortRef{AgentId{1}, 0}, PortRef{AgentId{3}, 0}},
        Wire{PortRef{AgentId{2}, 2}, PortRef{AgentId{3}, 1}}, Wire{PortRef{AgentId{3}, 2}, PortRef{AgentId{2}, 1}}}};

inline constexpr NetVerdict verdict = check_dataflow(crossed_adds);
static_assert(verdict.is_admitted(), verdict);

}  // namespace

int main() { return 0; }
