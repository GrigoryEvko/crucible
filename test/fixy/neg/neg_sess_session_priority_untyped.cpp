// A Resource states the priority of its session with a member of the
// type watch::priority.  A member of a plain integer type could be a
// count or an index that happens to carry the name, so the handle refuses
// it rather than read it as an order.

#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

#include <utility>

namespace {
namespace s = ::fixy::session;
struct Wire {
    static constexpr int session_priority = 2;
    int last_sent = 0;
};
}  // namespace

int main() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto head = s::mint_session<s::Send<int, s::End>>(ctx, Wire{});
    std::move(head).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
