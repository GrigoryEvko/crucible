// The permissioned mint starts a session with the set of the tokens that
// it consumes.  With no token the set is empty, and mint_session_handle
// is the mint for that case.  The gate refuses an empty set, so one
// session has one mint for each shape of its start.

#include <fixy/session/Handle.h>

#include <foundation/effects/Ctx.h>

#include <utility>

namespace s = ::fixy::session;
namespace eff = ::foundation::effects;

namespace no_tokens_fixture {
struct Ping {};
struct Wire {};
using SendsPing = s::Send<Ping, s::End>;
}  // namespace no_tokens_fixture

int main() {
    using namespace no_tokens_fixture;
    const eff::detail::ctx_witnesses::BgWitness ctx{eff::testing::bg()};
    auto [head, hold] = s::mint_permissioned_session<SendsPing>(ctx, Wire{});
    std::move(head).detach(s::detach_reason::TestInstrumentation{});
    static_cast<void>(std::move(hold).into_permissions());
    return 0;
}
