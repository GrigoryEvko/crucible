// A move assignment of a session handle from itself, through a local
// reference to it.  The move would keep nothing that the handle owes.  The
// kind self-move of the rule must-consume refuses it when the unit compiles,
// in each mode of the quarantine plugin.  The guard of the move assignment
// stays for an alias through two names, which the rule cannot see
// (self_move_through_two_names of test/fixy/test_session_handle_attacks.cpp).

#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

#include <utility>

namespace {
namespace s = ::fixy::session;
struct Msg {};
struct Wire {};
}  // namespace

int main() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto handle = s::mint_session<s::Send<Msg, s::End>>(ctx, Wire{});
    auto& same = handle;
    handle = std::move(same);
    std::move(handle).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
