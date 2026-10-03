// The round trip of duality on a node that no header registers.  An
// answer of true would let a gate that needs the involution admit an
// unknown combinator.  The dual of a type that is not a registered
// combinator stops the build with a message that names the type, so the
// round trip has no answer to give.  The first dual of the round trip is
// the one use, because a second dual of a refused dual gives a second
// error for the same cause.

#include <fixy/session/Protocol.h>

namespace {

namespace s = ::fixy::session;

// A protocol node that no header registers.
template <class T, class K>
struct Teleport {};

}  // namespace

static_assert(requires { typename s::dual_of_t<Teleport<int, s::End>>; });

int main() { return 0; }
