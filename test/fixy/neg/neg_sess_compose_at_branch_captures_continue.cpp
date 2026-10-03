// Composition at the exit branch of a loop.  The Loop stands on the
// spine above the choice, so the open Continue of the suffix binds it,
// and the exit branch loops.

#include <fixy/session/Protocol.h>

namespace {

namespace s = ::fixy::session;

struct Job {};
struct Retry {};
using Producer = s::Loop<s::Select<s::Send<Job, s::Continue>, s::End>>;

}  // namespace

// The requirement is the one use of the composition.  A refused
// composition names void, so a size of it gives a second error.
static_assert(requires { typename s::compose_at_branch_t<Producer, 1, s::Send<Retry, s::Continue>>; });

int main() { return 0; }
