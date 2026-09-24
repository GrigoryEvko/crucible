// A channel type that states no capacity gives the check no bound it can
// trust, so the check refuses it before it runs.

#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

struct PingReq {};
struct StopReq {};
using Early = s::Send<PingReq, s::Recv<StopReq, s::End>>;
using Late = s::Recv<StopReq, s::Send<PingReq, s::End>>;

// The end of a channel whose type says nothing about its buffer.
struct SilentEnd {};

}  // namespace

int main() {
    static_cast<void>(s::is_subtype_async_v<Early, Late, SilentEnd>);
    return 0;
}
