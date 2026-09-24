// A loop whose body is its own Continue has no action before the loop
// back, so its unfold never stops.  The old well-formedness check
// admitted it: the gate of mint_session_handle let it through, and the
// build then failed inside the handle unroll with an error that names
// no rule.  The guard rule refuses it at the gate.

#include <fixy/session/Handle.h>

namespace s = fixy::session;

namespace {
struct Wire {};
}  // namespace

using Spin = s::Loop<s::Continue>;

int main() {
    auto handle = s::mint_session_handle<Spin, Wire>(Wire{});
    (void)handle;
    return 0;
}
