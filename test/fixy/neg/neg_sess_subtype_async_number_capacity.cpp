// The capacity of the asynchronous check is a property of the channel,
// never a number that the caller states.  A number in place of the
// channel type does not compile, so a check at a capacity that no
// channel has cannot be written.

#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

struct PingReq {};
struct StopReq {};
using Early = s::Send<PingReq, s::Recv<StopReq, s::End>>;
using Late = s::Recv<StopReq, s::Send<PingReq, s::End>>;

}  // namespace

int main() {
    static_cast<void>(s::is_subtype_async_v<Early, Late, 4>);
    return 0;
}
