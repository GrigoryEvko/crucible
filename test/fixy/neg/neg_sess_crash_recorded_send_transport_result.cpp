// The recorder outside a crash-aware send passes a transport that
// returns a bool.  The recorder passes the result through, so the crash
// transport under it refuses the result.

#include <fixy/session/Recording.h>

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
    s::SessionEventLog log;
    auto handle = s::mint_recorded_session(s::mint_crash_session<Proto, Alice, Bob>(Wire{}, cell), log,
                                           s::RoleTagId{1}, s::RoleTagId{2});
    auto chosen = std::move(handle).template select<0>([](Wire&, std::size_t) noexcept {});
    auto sent = std::move(chosen).send(1, [](Wire&, int&&) noexcept { return true; });
    (void)std::move(sent.next).close();
    return 0;
}
