// A proof net keeps no cycle in any switching.  An axiom between the two
// premises of a tensor closes a cycle that each switching keeps, so
// check_proof_net refuses the net, which proves A tensor A~.

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

inline constexpr Net<2, 2, 2, 2> axiom_tensor{
    .signature =
        {.type_names = {"A", "AB"},
         .kinds = {{.name = "tensor",
                    .port_names = {"left", "right", "out"},
                    .port_types = {take(TypeId{0}), give(TypeId{0}), give(TypeId{1})},
                    .principal = 2},
                   {.name = "conclusion", .port_names = {"in"}, .port_types = {take(TypeId{1})}, .is_boundary = true}}},
    .agents = {KindId{0}, KindId{1}},
    .wires = {Wire{PortRef{AgentId{0}, 0}, PortRef{AgentId{0}, 1}},
              Wire{PortRef{AgentId{0}, 2}, PortRef{AgentId{1}, 0}}}};

inline constexpr NetVerdict verdict = check_proof_net(axiom_tensor);
static_assert(verdict.is_admitted(), verdict);

}  // namespace

int main() { return 0; }
