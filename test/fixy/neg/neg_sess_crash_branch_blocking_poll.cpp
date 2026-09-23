// A crash-aware Offer with a poll that returns the wire word itself.
// Such a poll waits inside the transport, where no crash is seen, so the
// crash branch never runs.  branch() refuses a poll that cannot report
// an empty queue.

#include <fixy/session/CrashTransport.h>

#include <cstddef>
#include <utility>

namespace s = fixy::session;

namespace {
struct Alice {};
struct Bob {};
struct Wire {};
using Proto = s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<Bob>, s::End>>;
}  // namespace

int main() {
    s::PeerCrashCell cell;
    auto handle = s::mint_crash_session<Proto, Alice, Bob>(Wire{}, cell);
    std::move(handle).branch([](Wire&) noexcept -> std::size_t { return 0; },
                             [](auto branch) noexcept { std::move(branch).detach(s::detach_reason::TestInstrumentation{}); });
    return 0;
}
