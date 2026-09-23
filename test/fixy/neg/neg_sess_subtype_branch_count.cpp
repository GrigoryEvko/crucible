// A Select subtype picks from no more branches than its supertype.  A
// subtype with an extra branch can pick a position the peer does not
// handle.  The refusal names the class of the failure and the pair.

#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

struct PingReq {};
struct StopReq {};
using Wide = s::Select<s::Send<PingReq, s::End>, s::Send<StopReq, s::End>>;
using Narrow = s::Select<s::Send<PingReq, s::End>>;

}  // namespace

int main() {
    s::assert_subtype_sync<Wide, Narrow>();
    return 0;
}
