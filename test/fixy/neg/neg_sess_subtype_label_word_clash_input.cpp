// The clash of one label word with two label keys, on an Offer.  The
// relation refuses it on an input choice as it does on an output choice.

#include <fixy/session/Projection.h>
#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

struct Bob {};
using First = decltype([] {});
using Second = decltype([] {});
using Sub = s::Offer<s::Recv<s::PeerMsg<Bob, First, int>, s::End>>;
using Super = s::Offer<s::Recv<s::PeerMsg<Bob, Second, int>, s::End>>;

}  // namespace

int main() {
    s::assert_subtype_sync<Sub, Super>();
    return 0;
}
