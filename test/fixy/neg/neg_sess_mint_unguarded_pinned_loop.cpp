// A wrapper is not an action, so a Continue under a VendorPinned that is
// the whole loop body is still unguarded.  The guard rule refuses it at
// the gate.  A check that admitted it would let the build fail later,
// inside the handle.

#include <fixy/session/Handle.h>

namespace s = fixy::session;

namespace {
struct Wire {};
}  // namespace

using Spin = s::Loop<s::VendorPinned<s::VendorBackend::NV, s::Continue>>;

int main() {
    auto handle = s::mint_session_handle<Spin, Wire>(Wire{});
    (void)handle;
    return 0;
}
