// Two closure types in one translation unit print one name, so their
// label words are equal while the label keys differ.  The peer cannot
// tell the two labels apart on the wire, and the relation refuses them.

#include <fixy/session/Projection.h>
#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

struct Bob {};
using First = decltype([] {});
using Second = decltype([] {});
using Sub = s::Select<s::Send<s::PeerMsg<Bob, First, int>, s::End>>;
using Super = s::Select<s::Send<s::PeerMsg<Bob, Second, int>, s::End>>;

}  // namespace

int main() {
    s::assert_subtype_sync<Sub, Super>();
    return 0;
}
