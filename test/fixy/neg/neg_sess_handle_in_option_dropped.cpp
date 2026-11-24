// A session handle at Send over a plain Resource is linear, so an Option of
// the handle is a linear sum.  A handle that ends inside the Option would drop
// its protocol with no step, and only the destructor would report it when the
// program runs.  So the rule must-consume refuses the end of the Option.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

#include <utility>

namespace neg_sess_handle_in_option_dropped_types {
struct Wire {
    int last_sent = 0;
};
}  // namespace neg_sess_handle_in_option_dropped_types

namespace {

namespace s = ::fixy::session;
using neg_sess_handle_in_option_dropped_types::Wire;

void keep_a_handle(::fixy::TestRunnerCtx const& ctx) noexcept {
    auto handle = s::mint_session<s::Send<int, s::End>>(ctx, Wire{});
    ::fixy::Option<decltype(handle)> held = ::fixy::Option<decltype(handle)>::some(std::move(handle));
    static_cast<void>(held.is_some());
}

}  // namespace

int main() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    keep_a_handle(ctx);
    return 0;
}
