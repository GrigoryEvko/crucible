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

int main() { return sizeof(s::compose_at_branch_t<Producer, 1, s::Send<Retry, s::Continue>>) == 0 ? 1 : 0; }
