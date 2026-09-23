// A client is compatible with a server when the client refines the dual
// of the server.  Two endpoints written from the same side both send
// first, and the check names the shape that differs.

#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

struct Query {};
struct Reply {};
using Client = s::Send<Query, s::Recv<Reply, s::End>>;
using SameSide = s::Send<Query, s::Recv<Reply, s::End>>;

}  // namespace

int main() {
    s::assert_compatible_client<Client, SameSide>();
    return 0;
}
