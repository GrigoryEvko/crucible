// A payload that points at a specialization of a template that is only
// declared here.  A unit that defines the template would read what it
// holds, so the public traits give no answer on it.  They stop the build.

#include <fixy/session/Payload.h>

namespace sess = ::fixy::session;

namespace {
template <class T>
struct OpaqueFrame;
}  // namespace

static_assert(sess::is_plain_payload_v<OpaqueFrame<int>*>);

int main() { return 0; }
