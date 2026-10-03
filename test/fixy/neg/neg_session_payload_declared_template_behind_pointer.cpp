// A payload that points at a specialization of a template that is only
// declared here.  A unit that defines the template would read what it
// holds, so the public traits give no answer on it.  They stop the build.

#include <fixy/session/Payload.h>

namespace sess = ::fixy::session;

namespace {
template <class T>
struct OpaqueFrame;
}  // namespace

// The trait is read and not asserted.  The gate of the trait stops the
// build, and an assertion on its value gives a second error for the same
// cause.
[[maybe_unused]] constexpr bool is_plain = sess::is_plain_payload_v<OpaqueFrame<int>*>;

int main() { return 0; }
