// Each port of a net has exactly one wire.  The output of drain has none, so
// check_wiring refuses the net and names the port, the agent and its node.

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

inline constexpr Net<2, 2, 2, 1> open_drain{
    .signature =
        {.type_names = {"Start", "TraceBatch"},
         .kinds = {{.name = "starter", .port_names = {"out"}, .port_types = {give(TypeId{0})}},
                   {.name = "drain", .port_names = {"in", "out"}, .port_types = {take(TypeId{0}), give(TypeId{1})}}}},
    .agents = {KindId{0}, KindId{1}},
    .wires = {Wire{PortRef{AgentId{0}, 0}, PortRef{AgentId{1}, 0}}}};

inline constexpr NetVerdict verdict = check_wiring(open_drain);
static_assert(verdict.is_admitted(), verdict);

}  // namespace

int main() { return 0; }
