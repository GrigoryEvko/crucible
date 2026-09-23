// A positional Offer against a keyed Offer.  The peer of the keyed Offer
// sends label words, and the positional Offer dispatches on positions,
// so the relation refuses the pair in this direction too.

#include <fixy/session/Projection.h>
#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

struct Bob {};
struct Hello {};
using Sub = s::Offer<s::Recv<int, s::End>>;
using Super = s::Offer<s::Recv<s::PeerMsg<Bob, Hello, int>, s::End>>;

}  // namespace

int main() {
    s::assert_subtype_sync<Sub, Super>();
    return 0;
}
