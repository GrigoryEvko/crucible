// seal_net is the only door to a SealedNet, so the fault that a sealed net
// holds is the fault of the wiring of its net.  A caller that builds a sealed
// net by hand does not compile: the constructor of SealedNet is private.

#include <foundation/algebra/net/Net.h>

namespace {

namespace net = ::foundation::algebra::net;

inline constexpr net::TypeId item{0};

inline constexpr net::Net<1, 2, 2, 1> pair{
    .signature = {.type_names = {"Item"},
                  .kinds = {{.name = "source", .port_names = {"out"}, .port_types = {net::give(item)}},
                            {.name = "sink", .port_names = {"in"}, .port_types = {net::take(item)}}}},
    .agents = {net::KindId{0}, net::KindId{1}},
    .wires = {net::Wire{net::PortRef{net::AgentId{0}, 0}, net::PortRef{net::AgentId{1}, 0}}}};

inline constexpr net::SealedNet<1, 2, 2, 1> forged{pair};

}  // namespace

int main() { return forged.wiring_fault().is_fault() ? 1 : 0; }
