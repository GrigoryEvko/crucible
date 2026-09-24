// A crash-aware endpoint sends a label whose branch opens with the
// payload of that label, and crashes before the payload.  The label and
// the payload are one message, so the peer would take the label and wait
// in a branch that has no crash branch.  crash() refuses the position.

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
    s::PeerCrashCell peer_cell;
    s::PeerCrashCell own_cell;
    auto handle = s::mint_crash_session<Proto, Alice, Bob>(Wire{}, peer_cell);
    auto half_sent = std::move(handle).template select<0>([](Wire&, std::size_t) noexcept {});
    (void)std::move(half_sent).crash(s::CrashCause::Abort, s::mint_crash_reporter(own_cell));
    return 0;
}
