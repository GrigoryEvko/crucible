// is_dual_involutive answered true for every type the old primary did
// not know, so a gate that needs the involution admitted any unknown
// combinator.  The trait is now the round trip itself, and the dual of
// a type that is not a registered combinator stops the build with a
// message that names the type.
//
// The assertion below held before the repair: the old primary answered
// true.  It is the attack.

#include <fixy/session/Protocol.h>

namespace {

namespace s = ::fixy::session;

// A protocol node that no header registers.
template <class T, class K>
struct Teleport {};

}  // namespace

static_assert(s::is_dual_involutive_v<Teleport<int, s::End>>);

int main() { return 0; }
