// Rule r-↯ lets only a role outside the reliable set crash.  A role
// that the deployment declared reliable cannot call crash().

#include <fixy/session/CrashTransport.h>

namespace s = fixy::session;

namespace {
struct Alice {};
struct Bob {};
struct Wire {};
}  // namespace

using Proto = s::Select<s::Send<int, s::End>>;

int main() {
    s::PeerCrashCell watched;
    s::PeerCrashCell announce;
    auto handle = s::mint_crash_session<Proto, Alice, Bob, s::ReliableSet<Alice>>(Wire{}, watched);
    auto resource = std::move(handle).crash(s::CrashCause::Abort, announce);
    (void)resource;
    return 0;
}
