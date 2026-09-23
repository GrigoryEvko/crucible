// An input never moves ahead of an output: the subtype would wait for a
// message the peer sends only after it receives one, and both wait.

#include <fixy/session/Subtype.h>

#include <cstddef>

namespace {

namespace s = ::fixy::session;

struct PingReq {};
struct StopReq {};
using Early = s::Send<PingReq, s::Recv<StopReq, s::End>>;
using Late = s::Recv<StopReq, s::Send<PingReq, s::End>>;
struct EightSlots {
    static constexpr std::size_t channel_capacity = 8;
};

}  // namespace

int main() {
    s::assert_subtype_async<Late, Early, EightSlots>();
    return 0;
}
