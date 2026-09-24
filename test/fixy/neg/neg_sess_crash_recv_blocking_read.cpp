// A crash-aware reception with a read that returns the payload itself.
// Such a read waits inside the transport, where no crash is seen, so a
// dead peer blocks it for ever.  recv() refuses a read that cannot report
// an empty queue.

#include <fixy/session/CrashTransport.h>

#include <utility>

namespace s = fixy::session;

namespace {
struct Alice {};
struct Bob {};
struct Wire {};
using Proto = s::Recv<int, s::End>;
}  // namespace

int main() {
    s::PeerCrashCell cell;
    auto handle = s::mint_crash_session<Proto, Alice, Bob, s::ReliableSet<Bob>>(Wire{}, cell);
    auto [value, end] = std::move(handle).recv([](Wire&) noexcept { return 1; });
    (void)value;
    (void)std::move(end).close();
    return 0;
}
