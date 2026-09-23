// An input never moves ahead of an output: the subtype would wait for a
// message the peer sends only after it receives one, and both wait.

#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

struct PingReq {};
struct StopReq {};
using Early = s::Send<PingReq, s::Recv<StopReq, s::End>>;
using Late = s::Recv<StopReq, s::Send<PingReq, s::End>>;

}  // namespace

int main() {
    s::assert_subtype_async<Late, Early, 8>();
    return 0;
}
