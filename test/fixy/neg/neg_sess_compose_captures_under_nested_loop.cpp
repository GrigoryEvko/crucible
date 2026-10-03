// The Loop that captures the Continue of the suffix stands below a step
// of the prefix, and the suffix sends before its Continue.  The capture
// is the same: the End that composition replaces stands under the Loop.

#include <fixy/session/Protocol.h>

namespace {

namespace s = ::fixy::session;

struct Hello {};
struct Job {};
struct Retry {};
using Session = s::Send<Hello, s::Loop<s::Select<s::Send<Job, s::Continue>, s::End>>>;
using Again = s::Send<Retry, s::Continue>;

}  // namespace

// The requirement is the one use of the composition.  A refused
// composition names void, so a size of it gives a second error.
static_assert(requires { typename s::compose_t<Session, Again>; });

int main() { return 0; }
