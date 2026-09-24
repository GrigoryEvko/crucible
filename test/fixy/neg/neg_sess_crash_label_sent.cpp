// The crash label is a pseudo-message that no endpoint can send (rule
// 1).  The plain dual of an endpoint with a crash branch sends it, so
// the handle factory refuses that dual.  The peer uses crash_dual_t.

#include <fixy/session/CrashTransport.h>

namespace s = fixy::session;

namespace {
struct Alice {};
struct Bob {};
struct Wire {};
}  // namespace

using Guarded = s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<Bob>, s::End>>;

int main() {
    auto handle = s::mint_session_handle<s::dual_of_t<Guarded>, Wire>(Wire{});
    (void)handle;
    return 0;
}
