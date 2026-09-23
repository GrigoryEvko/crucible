// The callback entry point takes back only an End handle of its own
// session.  This body drops the handle it was given, mints a new session
// over a new resource, walks that one to End and returns it.  The new
// handle has no brand, so the body gate refuses it, and the handle the
// body was given cannot be replaced.

#include <fixy/session/Handle.h>

#include <utility>

namespace {
namespace s = ::fixy::session;
struct Ping {};
struct Wire {};
using Proto = s::Send<Ping, s::End>;
}  // namespace

int main() {
    auto back = s::with_session<Proto>(Wire{}, [](auto head) noexcept {
        std::move(head).detach(s::detach_reason::TestInstrumentation{});
        auto other = s::mint_session_handle<Proto>(Wire{});
        return std::move(other).send(Ping{}, [](Wire&, Ping&&) noexcept {});
    });
    (void)back;
    return 0;
}
