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

int main() { return sizeof(s::compose_t<Session, Again>) == 0 ? 1 : 0; }
