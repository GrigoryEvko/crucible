// std::make_unique puts a session handle on the heap, and
// unique_ptr::release then gives up its only owner.  The handle deletes
// its class-scope operator new, so std::make_unique of a handle is
// refused inside the library.

#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

#include <memory>
#include <utility>

namespace {
namespace s = ::fixy::session;
struct Msg {};
struct Wire {};
using Head = decltype(s::mint_session<s::Send<Msg, s::End>>(std::declval<::fixy::TestRunnerCtx const&>(), Wire{}));
}  // namespace

int main() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto owner = std::make_unique<Head>(s::mint_session<s::Send<Msg, s::End>>(ctx, Wire{}));
    std::move(*owner).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
