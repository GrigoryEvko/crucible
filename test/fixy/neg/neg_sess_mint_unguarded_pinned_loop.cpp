// A wrapper is not an action, so a Continue under a VendorPinned that is
// the whole loop body is still unguarded.  The old check admitted it,
// and the build failed later inside the handle.  The guard rule refuses
// it at the gate.

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
