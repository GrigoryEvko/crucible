// Stop is the runtime type of a crashed endpoint (LMCS 2025 Fig. 6).  A
// protocol written at design time never contains it.

#include <fixy/session/CrashTransport.h>

namespace s = fixy::session;

namespace {
struct Alice {};
struct Bob {};
struct Wire {};
}  // namespace

using Proto = s::Send<int, s::Stop>;

int main() {
    auto handle = s::mint_session_handle<Proto, Wire>(Wire{});
    (void)handle;
    return 0;
}
