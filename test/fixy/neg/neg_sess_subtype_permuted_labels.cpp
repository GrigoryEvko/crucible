// A Select with the same labels in another order is another protocol,
// because the handle sends the position of a branch as its wire label.
// The relation compares the branches position by position, and names
// the first pair of messages whose labels differ.

#include <fixy/session/Projection.h>
#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

struct Bob {};
struct Hello {};
struct Bye {};
using Projected = s::Select<s::Send<s::PeerMsg<Bob, Hello, int>, s::End>, s::Send<s::PeerMsg<Bob, Bye, int>, s::End>>;
using Permuted = s::Select<s::Send<s::PeerMsg<Bob, Bye, int>, s::End>, s::Send<s::PeerMsg<Bob, Hello, int>, s::End>>;

}  // namespace

int main() {
    s::assert_subtype_sync<Permuted, Projected>();
    return 0;
}
