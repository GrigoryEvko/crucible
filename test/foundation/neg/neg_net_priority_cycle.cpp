// A priority for each action exists only when no cycle of waits does.  In
// CrossedRing each process waits on its left neighbour first, so
// check_priorities refuses the system and names each wait of the cycle.

#include <foundation/algebra/net/Priority.h>

namespace {

using foundation::algebra::net::Action;
using foundation::algebra::net::ChannelId;
using foundation::algebra::net::ChannelSide;
using foundation::algebra::net::NetVerdict;
using foundation::algebra::net::ProcessId;
using foundation::algebra::net::ProcessSystem;

inline constexpr ProcessSystem<3, 3, 6> crossed_ring{
    .process_names = {"P", "Q", "R"},
    .channel_names = {"pq", "qr", "rp"},
    .actions = {Action{ProcessId{0}, ChannelId{2}, ChannelSide::Second, false, true, 0},
                Action{ProcessId{0}, ChannelId{0}, ChannelSide::First, true, true, 0},
                Action{ProcessId{1}, ChannelId{0}, ChannelSide::Second, false, true, 0},
                Action{ProcessId{1}, ChannelId{1}, ChannelSide::First, true, true, 0},
                Action{ProcessId{2}, ChannelId{1}, ChannelSide::Second, false, true, 0},
                Action{ProcessId{2}, ChannelId{2}, ChannelSide::First, true, true, 0}}};

inline constexpr NetVerdict verdict = check_priorities(crossed_ring);
static_assert(verdict.is_admitted(), verdict);

}  // namespace

int main() { return 0; }
