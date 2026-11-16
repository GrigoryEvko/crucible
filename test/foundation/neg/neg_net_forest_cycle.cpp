// The agents and the wires of a composition form a forest.  P and Q share
// two sessions, so the second wire closes a cycle, and check_forest refuses
// the net and names the wire and its two ports.

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

inline constexpr Net<2, 2, 2, 2> two_sessions{
    .signature =
        {.type_names = {"S1", "S2"},
         .kinds = {{.name = "P", .port_names = {"s1", "s2"}, .port_types = {give(TypeId{0}), take(TypeId{1})}},
                   {.name = "Q", .port_names = {"s1", "s2"}, .port_types = {take(TypeId{0}), give(TypeId{1})}}}},
    .agents = {KindId{0}, KindId{1}},
    .wires = {Wire{PortRef{AgentId{0}, 0}, PortRef{AgentId{1}, 0}},
              Wire{PortRef{AgentId{0}, 1}, PortRef{AgentId{1}, 1}}}};

inline constexpr NetVerdict verdict = check_forest(two_sessions);
static_assert(verdict.is_admitted(), verdict);

}  // namespace

int main() { return 0; }
