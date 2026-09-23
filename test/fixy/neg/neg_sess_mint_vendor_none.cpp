// VendorBackend::None names no kernel, so a session pinned to it runs
// against nothing.  The old check admitted it at the gate.  The value
// filter of the VendorPinned registration refuses it there.

#include <fixy/session/Handle.h>

namespace s = fixy::session;

namespace {
struct Ping {};
struct Wire {};
}  // namespace

using Nowhere = s::VendorPinned<s::VendorBackend::None, s::Send<Ping, s::End>>;

int main() {
    auto handle = s::mint_session_handle<Nowhere, Wire>(Wire{});
    (void)handle;
    return 0;
}
