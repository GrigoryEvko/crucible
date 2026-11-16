// A system is live only when each cycle of waits holds a token.  In
// CrossedRing each process waits on its left neighbour first, over a
// rendezvous, so check_liveness refuses the system and names each step of
// the cycle.

#include <foundation/algebra/net/Priority.h>

namespace {

using foundation::algebra::net::Action;
using foundation::algebra::net::ChannelId;
using foundation::algebra::net::Move;
using foundation::algebra::net::NetVerdict;
using foundation::algebra::net::ProcessId;
using foundation::algebra::net::ProcessSystem;

inline constexpr ProcessSystem<3, 3, 6> crossed_ring{
    .process_names = {"P", "Q", "R"},
    .channels = {{.name = "pq"}, {.name = "qr"}, {.name = "rp"}},
    .actions = {Action{.process = ProcessId{0}, .channel = ChannelId{2}, .move = Move::Receive},
                Action{.process = ProcessId{0}, .channel = ChannelId{0}, .move = Move::Send},
                Action{.process = ProcessId{1}, .channel = ChannelId{0}, .move = Move::Receive},
                Action{.process = ProcessId{1}, .channel = ChannelId{1}, .move = Move::Send},
                Action{.process = ProcessId{2}, .channel = ChannelId{1}, .move = Move::Receive},
                Action{.process = ProcessId{2}, .channel = ChannelId{2}, .move = Move::Send}}};

inline constexpr NetVerdict verdict = check_liveness(crossed_ring);
static_assert(verdict.is_admitted(), verdict);

}  // namespace

int main() { return 0; }
