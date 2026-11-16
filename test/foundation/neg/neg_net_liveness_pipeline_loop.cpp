// A loop of a pipeline is live only when its cycle holds a token.  The Vigil
// waits on the count that publish advances before it pushes into the ring,
// so the cycle through the five processes holds no token, whatever the
// capacity of each ring.

#include <foundation/algebra/net/Priority.h>

namespace {

using foundation::algebra::net::Action;
using foundation::algebra::net::ChannelId;
using foundation::algebra::net::Move;
using foundation::algebra::net::NetVerdict;
using foundation::algebra::net::ProcessId;
using foundation::algebra::net::ProcessSystem;

inline constexpr ProcessId vigil{0};
inline constexpr ProcessId drain{1};
inline constexpr ProcessId detect{2};
inline constexpr ProcessId build{3};
inline constexpr ProcessId publish{4};
inline constexpr ChannelId ring{0};
inline constexpr ChannelId trace{1};
inline constexpr ChannelId work{2};
inline constexpr ChannelId graphs{3};
inline constexpr ChannelId processed{4};

inline constexpr ProcessSystem<5, 5, 10> pipeline_waits_first{
    .process_names = {"vigil", "drain", "detect", "build", "publish"},
    .channels = {{.name = "ring", .capacity = 64},
                 {.name = "trace", .capacity = 64},
                 {.name = "work", .capacity = 64},
                 {.name = "graphs", .capacity = 64},
                 {.name = "processed", .capacity = 1}},
    .actions = {Action{.process = vigil, .channel = processed, .move = Move::Receive},
                Action{.process = vigil, .channel = ring, .move = Move::Send},
                Action{.process = drain, .channel = ring, .move = Move::Receive},
                Action{.process = drain, .channel = trace, .move = Move::Send},
                Action{.process = detect, .channel = trace, .move = Move::Receive},
                Action{.process = detect, .channel = work, .move = Move::Send},
                Action{.process = build, .channel = work, .move = Move::Receive},
                Action{.process = build, .channel = graphs, .move = Move::Send},
                Action{.process = publish, .channel = graphs, .move = Move::Receive},
                Action{.process = publish, .channel = processed, .move = Move::Send}}};

inline constexpr NetVerdict verdict = check_liveness(pipeline_waits_first);
static_assert(verdict.is_admitted(), verdict);

}  // namespace

int main() { return 0; }
