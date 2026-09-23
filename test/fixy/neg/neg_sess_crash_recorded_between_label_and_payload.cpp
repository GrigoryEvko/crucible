// The recorder outside a crash-aware endpoint forwards a crash that
// falls between a label and the payload of its branch.  The recorder adds
// no route around the crash transport, which refuses the position.

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
    s::PeerCrashCell peer_cell;
    s::PeerCrashCell own_cell;
    s::SessionEventLog log;
    auto handle = s::mint_recorded_session(s::mint_crash_session<Proto, Alice, Bob>(Wire{}, peer_cell), log,
                                           s::RoleTagId{1}, s::RoleTagId{2});
    auto half_sent = std::move(handle).template select<0>([](Wire&, std::size_t) noexcept {});
    (void)std::move(half_sent).crash(s::CrashCause::Abort, own_cell);
    return 0;
}
