// A keyed Select of the subtype sends a label that the Select of the
// supertype does not send.  Branches pair by label, in any order, so the
// relation finds the extra label wherever it stands and refuses it.

#include <fixy/session/Projection.h>
#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

struct Bob {};
struct Hello {};
struct Bye {};
struct Retry {};
using Sub = s::Select<s::Send<s::PeerMsg<Bob, Retry, int>, s::End>, s::Send<s::PeerMsg<Bob, Hello, int>, s::End>>;
using Super = s::Select<s::Send<s::PeerMsg<Bob, Bye, int>, s::End>, s::Send<s::PeerMsg<Bob, Hello, int>, s::End>>;

}  // namespace

int main() {
    s::assert_subtype_sync<Sub, Super>();
    return 0;
}
