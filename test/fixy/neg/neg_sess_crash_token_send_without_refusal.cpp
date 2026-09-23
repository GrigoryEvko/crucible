// A crash-aware session relays a token with a transport that returns
// void.  The peer can crash between the crash check and the write, and
// the token then goes into a queue that no one reads.  The send refuses
// a transport that cannot give the token back.

#include <fixy/session/CrashTransport.h>

#include <cstddef>
#include <optional>
#include <utility>

namespace s = fixy::session;
namespace fp = foundation::permissions;

namespace {
struct Alice {};
struct Bob {};
struct Wire {};
struct Region {
    using permission_row = foundation::effects::Row<>;
};
using Token = s::Transferable<int, Region>;
using Relay = s::Recv<Token, s::Select<s::Send<Token, s::End>>>;
}  // namespace

int main() {
    s::PeerCrashCell cell;
    auto handle = s::mint_crash_session<Relay, Alice, Bob, s::ReliableSet<Bob>>(Wire{}, cell);
    auto [token, reply] = std::move(handle).recv(
        [](Wire&) noexcept -> std::optional<Token> { return Token{1, fp::mint_permission_root<Region>()}; });
    auto chosen = std::move(reply).template select<0>([](Wire&, std::size_t) noexcept {});
    auto sent = std::move(chosen).send(std::move(token), [](Wire&, Token&&) noexcept {});
    (void)std::move(sent.next).close();
    return 0;
}
