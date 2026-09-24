// A producer that never stops against a producer that can stop.  The
// narrower Select drops the only exit of the loop, so the consumer that
// waits for the stop never gets it.  The subtype removes an exit.

#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

struct Job {};
struct StopCmd {};
using Stopping = s::Loop<s::Select<s::Send<Job, s::Continue>, s::Send<StopCmd, s::End>>>;
using Endless = s::Loop<s::Select<s::Send<Job, s::Continue>>>;

}  // namespace

int main() {
    s::assert_subtype_sync<Endless, Stopping>();
    return 0;
}
