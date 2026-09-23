// The peer walk of strip_peers_t knows the local combinators of
// Protocol.h and nothing else.  A type it does not know stops the build,
// so no combinator can pass through the binary view with its peer
// unchecked.

#include <fixy/session/Projection.h>

namespace {

namespace s = ::fixy::session;

struct Unknown {};

using Refused = s::strip_peers_t<Unknown>;

}  // namespace

int main() { return 0; }
