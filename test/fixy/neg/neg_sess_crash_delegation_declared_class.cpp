// The delegation query asked about a payload that points at a class that
// is only declared here.  A unit that defines the class would read it
// and could answer differently, so the query gives no value.  It stops
// the build.

#include <fixy/session/Payload.h>

namespace s = fixy::session;

namespace {
struct OpaqueChannel;

struct Frame {
    int sequence = 0;
    OpaqueChannel* channel = nullptr;
};

static_assert(!s::payload_conveys_delegation_v<Frame>);
}  // namespace

int main() { return 0; }
