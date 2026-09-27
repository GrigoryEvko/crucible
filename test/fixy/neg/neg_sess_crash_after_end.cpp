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
    s::PeerCrashCell own;
    const ::foundation::effects::detail::ctx_witnesses::BgWitness ctx{::foundation::effects::testing::bg()};
    auto handle = s::mint_crash_session<Proto, Alice, Bob>(ctx, Wire{}, watched, s::mint_crash_writer(own));
    auto next = std::move(handle).select<0>([](Wire&, std::size_t) noexcept { return true; });
    auto [at_end, undelivered] = std::move(next).send(1, [](Wire&, int&) noexcept { return true; });
    (void)undelivered;
    auto resource = std::move(at_end).crash(s::CrashCause::Abort);
    (void)resource;
    return 0;
}
