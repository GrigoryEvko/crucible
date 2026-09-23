// Rule r-↯ applies to a process that has not ended.  At End there is no
// crash() to call.

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
    auto handle = s::mint_crash_session<Proto, Alice, Bob>(Wire{}, watched);
    auto next = std::move(handle).select<0>([](Wire&, std::size_t) noexcept {});
    auto [at_end, undelivered] = std::move(next).send(1, [](Wire&, int&&) noexcept {});
    (void)undelivered;
    auto resource = std::move(at_end).crash(s::CrashCause::Abort, announce);
    (void)resource;
    return 0;
}
