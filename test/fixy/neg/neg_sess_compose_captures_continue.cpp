// Composition with a bare Continue puts the Continue where the End of the
// loop stood.  The Continue binds that Loop, so the exit of the loop
// becomes a loop-back and the protocol can never end.  Composition
// refuses the capture.

#include <fixy/session/Protocol.h>

namespace {

namespace s = ::fixy::session;

struct Job {};
using Producer = s::Loop<s::Select<s::Send<Job, s::Continue>, s::End>>;

}  // namespace

// The requirement is the one use of the composition.  A refused
// composition names void, so a size of it gives a second error.
static_assert(requires { typename s::compose_t<Producer, s::Continue>; });

int main() { return 0; }
