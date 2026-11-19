// The fault that a sealed net holds stays the fault of its net, because no
// caller can change the net of a SealedNet.  A write through value() does
// not compile: the reference is const.  Each find function on a sealed net then
// reads each port through a wire that find_wiring_fault admitted.

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

// Seals the pair, and then points its wire past the ports of the source.
[[nodiscard]] net::NetFault miswire_after_seal(net::Net<1, 2, 2, 1> const& graph) noexcept {
    auto sealed = net::seal_net(graph);
    sealed.value().wires[0].left.port = 7;
    return net::find_forest_fault(sealed);
}

}  // namespace

int main() { return miswire_after_seal(pair).is_fault() ? 1 : 0; }
