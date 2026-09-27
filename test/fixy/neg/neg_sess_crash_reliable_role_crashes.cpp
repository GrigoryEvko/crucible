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
    s::PeerCrashCell own;
    const ::foundation::effects::detail::ctx_witnesses::BgWitness ctx{::foundation::effects::testing::bg()};
    auto handle = s::mint_crash_session<Proto, Alice, Bob, s::ReliableSet<Alice>>(ctx, Wire{}, watched, s::mint_crash_writer(own));
    auto resource = std::move(handle).crash(s::CrashCause::Abort);
    (void)resource;
    return 0;
}
