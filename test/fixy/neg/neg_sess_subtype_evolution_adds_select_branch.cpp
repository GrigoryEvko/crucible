// A new protocol version that adds a Select branch is not a refinement
// of the old one: the new client can pick a branch the old server does
// not handle.

#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

struct PingReq {};
struct StopReq {};
using OldClient = s::Loop<s::Select<s::Send<PingReq, s::Continue>>>;
using NewClient = s::Loop<s::Select<s::Send<PingReq, s::Continue>, s::Send<StopReq, s::End>>>;

}  // namespace

int main() {
    s::check_protocol_evolution<OldClient, NewClient>();
    return 0;
}
