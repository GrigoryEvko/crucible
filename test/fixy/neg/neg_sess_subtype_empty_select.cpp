// An empty Select is not well-formed.  Under the branch rule it refines
// every Select, and a substitute of that type never sends, so the
// session deadlocks.  The relation refuses it as an operand and names
// the operand.

#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

struct PingReq {};
using Empty = s::Select<>;
using One = s::Select<s::Send<PingReq, s::End>>;

}  // namespace

int main() {
    s::assert_subtype_sync<Empty, One>();
    return 0;
}
