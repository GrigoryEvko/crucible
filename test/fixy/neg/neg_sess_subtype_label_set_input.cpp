// A keyed Offer of the supertype receives a label that the Offer of the
// subtype does not receive.  The subtype must receive each label of the
// supertype, wherever it stands.

#include <fixy/session/Projection.h>
#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

struct Bob {};
struct Hello {};
struct Bye {};
using Sub = s::Offer<s::Sender<Bob>, s::Recv<s::PeerMsg<Bob, Hello, int>, s::End>>;
using Super = s::Offer<s::Sender<Bob>, s::Recv<s::PeerMsg<Bob, Bye, int>, s::End>, s::Recv<s::PeerMsg<Bob, Hello, int>, s::End>>;

}  // namespace

int main() {
    s::assert_subtype_sync<Sub, Super>();
    return 0;
}
