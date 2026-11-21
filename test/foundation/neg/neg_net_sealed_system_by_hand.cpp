// seal_system is the only door to a SealedSystem, so the fault that a sealed
// system holds is the fault of the form of its system.  A caller that builds
// a sealed system by hand does not compile: the constructor of SealedSystem
// is private.

#include <foundation/algebra/net/Priority.h>

namespace {

namespace net = ::foundation::algebra::net;

inline constexpr net::ProcessSystem<2, 1, 2> pair{
    .process_names = {"P", "Q"},
    .channels = {{.name = "queue", .capacity = 1}},
    .actions = {net::Action{.process = net::ProcessId{0}, .channel = net::ChannelId{0}, .move = net::Move::Send},
                net::Action{.process = net::ProcessId{1}, .channel = net::ChannelId{0}, .move = net::Move::Receive}}};

inline constexpr net::SealedSystem<2, 1, 2> forged{pair};

}  // namespace

int main() { return forged.form_fault().is_fault() ? 1 : 0; }
