// The fault that a sealed system holds stays the fault of its system, because
// no caller can change the system of a SealedSystem.  A write through value()
// does not compile: the reference is const.  find_liveness_fault on a sealed
// system then reads each action inside the system that the seal checked.

#include <foundation/algebra/net/Priority.h>

namespace {

namespace net = ::foundation::algebra::net;

inline constexpr net::ProcessSystem<2, 1, 2> pair{
    .process_names = {"P", "Q"},
    .channels = {{.name = "queue", .capacity = 1}},
    .actions = {net::Action{.process = net::ProcessId{0}, .channel = net::ChannelId{0}, .move = net::Move::Send},
                net::Action{.process = net::ProcessId{1}, .channel = net::ChannelId{0}, .move = net::Move::Receive}}};

// Seals the pair, and then points its receive past the channels.
[[nodiscard]] net::LivenessFault move_after_seal(net::ProcessSystem<2, 1, 2> const& protocol) noexcept {
    auto sealed = net::seal_system(protocol);
    sealed.value().actions[1].channel = net::ChannelId{7};
    return net::find_liveness_fault(sealed);
}

}  // namespace

int main() { return move_after_seal(pair).is_fault() ? 1 : 0; }
