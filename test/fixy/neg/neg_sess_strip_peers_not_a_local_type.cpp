// The peer walk of strip_peers_t knows the local combinators of
// Protocol.h and nothing else.  A type it does not know stops the build,
// so no combinator can pass through the binary view with its peer
// unchecked.

#include <fixy/session/Projection.h>

namespace {

namespace s = ::fixy::session;

struct Unknown {};

// The requirement is the one use of the alias, so the refusal is the only
// error.
static_assert(requires { typename s::strip_peers_t<Unknown>; });

}  // namespace

int main() { return 0; }
