// A send waits for a free slot of its channel.  P sends two messages on c
// and then one on d, and Q waits on d first.  The channel c holds one
// message, so the second send waits for a receive that waits on d, and the
// system deadlocks.  A capacity of two admits the same system.

#include <foundation/algebra/net/Priority.h>

namespace {

using foundation::algebra::net::Action;
using foundation::algebra::net::ChannelId;
using foundation::algebra::net::Move;
using foundation::algebra::net::NetVerdict;
using foundation::algebra::net::ProcessId;
using foundation::algebra::net::ProcessSystem;

inline constexpr ProcessId process_p{0};
inline constexpr ProcessId process_q{1};
inline constexpr ChannelId channel_c{0};
inline constexpr ChannelId channel_d{1};

inline constexpr ProcessSystem<2, 2, 6> full_buffer{
    .process_names = {"P", "Q"},
    .channels = {{.name = "c", .capacity = 1}, {.name = "d", .capacity = 1}},
    .actions = {Action{.process = process_p, .channel = channel_c, .move = Move::Send},
                Action{.process = process_p, .channel = channel_c, .move = Move::Send},
                Action{.process = process_p, .channel = channel_d, .move = Move::Send},
                Action{.process = process_q, .channel = channel_d, .move = Move::Receive},
                Action{.process = process_q, .channel = channel_c, .move = Move::Receive},
                Action{.process = process_q, .channel = channel_c, .move = Move::Receive}}};

inline constexpr NetVerdict verdict = check_liveness(full_buffer);
static_assert(verdict.is_admitted(), verdict);

}  // namespace

int main() { return 0; }
