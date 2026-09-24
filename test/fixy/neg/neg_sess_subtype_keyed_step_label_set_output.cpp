// A keyed Send is the Select of its one branch.  It sends the label L2,
// which the Select of the supertype, of the labels L0 and L1, does not
// send, so it does not refine that Select.

#include <fixy/session/Projection.h>
#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

struct Bob {};
struct L0 {};
struct L1 {};
struct L2 {};
using Sub = s::Send<s::PeerMsg<Bob, L2, int>, s::End>;
using Super = s::Select<s::Send<s::PeerMsg<Bob, L0, int>, s::End>, s::Send<s::PeerMsg<Bob, L1, int>, s::End>>;

}  // namespace

int main() {
    s::assert_subtype_sync<Sub, Super>();
    return 0;
}
