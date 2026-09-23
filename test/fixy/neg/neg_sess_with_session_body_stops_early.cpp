// The callback entry point owns the handle and closes the handle that
// the body returns.  This body returns the handle before the receive, so
// there is no End handle to close.  The body gate refuses it.

#include <fixy/session/Handle.h>

#include <utility>

namespace {
namespace s = ::fixy::session;
struct Ping {};
struct Pong {};
struct Wire {};
using Proto = s::Send<Ping, s::Recv<Pong, s::End>>;
}  // namespace

int main() {
    auto back = s::with_session<Proto>(Wire{}, [](auto head) noexcept {
        return std::move(head).send(Ping{}, [](Wire&, Ping&&) noexcept {});
    });
    (void)back;
    return 0;
}
