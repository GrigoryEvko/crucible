// Two messages sent ahead need a buffer of two in each direction.  On a
// channel of capacity one, the subtype and the peer both wait on a full
// buffer, and the session deadlocks.  The check refuses capacity one.

#include <fixy/session/Subtype.h>

#include <cstddef>

namespace {

namespace s = ::fixy::session;

struct PingReq {};
struct StopReq {};
using Early = s::Send<PingReq, s::Send<PingReq, s::Recv<StopReq, s::Recv<StopReq, s::End>>>>;
using Late = s::Recv<StopReq, s::Recv<StopReq, s::Send<PingReq, s::Send<PingReq, s::End>>>>;
struct OneSlot {
    static constexpr std::size_t channel_capacity = 1;
};

}  // namespace

int main() {
    s::assert_subtype_async<Early, Late, OneSlot>();
    return 0;
}
