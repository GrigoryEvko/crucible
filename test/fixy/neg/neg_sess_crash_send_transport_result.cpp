// A crash-aware send with a transport that returns a bool.  A result
// other than void or std::optional<T> says nothing the decorator can
// read about the write, so send() refuses it.

#include <fixy/session/CrashTransport.h>

#include <cstddef>
#include <utility>

namespace s = fixy::session;

namespace {
struct Alice {};
struct Bob {};
struct Wire {};
using Proto = s::Select<s::Send<int, s::End>>;
}  // namespace

int main() {
    s::PeerCrashCell cell;
    auto handle = s::mint_crash_session<Proto, Alice, Bob>(Wire{}, cell);
    auto chosen = std::move(handle).template select<0>([](Wire&, std::size_t) noexcept {});
    auto sent = std::move(chosen).send(1, [](Wire&, int&&) noexcept { return true; });
    (void)std::move(sent.next).close();
    return 0;
}
