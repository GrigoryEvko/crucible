// A keyed Select puts label words on the wire, and a positional Select
// puts positions.  The two cannot face one peer, so a keyed choice never
// refines a positional one.

#include <fixy/session/Projection.h>
#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

struct Bob {};
struct Hello {};
using Sub = s::Select<s::Send<s::PeerMsg<Bob, Hello, int>, s::End>>;
using Super = s::Select<s::Send<int, s::End>>;

}  // namespace

int main() {
    s::assert_subtype_sync<Sub, Super>();
    return 0;
}
