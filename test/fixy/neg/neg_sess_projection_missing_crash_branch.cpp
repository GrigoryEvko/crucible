// Bob receives from Alice, who is not reliable, and the transmission
// has no crash branch.  If Alice crashes, Bob waits for ever.

#include <fixy/session/Projection.h>

namespace {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

struct Alice {};
struct Bob {};
struct Carol {};
struct Ping {};
struct Pong {};

using NoCrashBranch = g::Msg<Alice, Bob, Ping, int, g::End>;

constexpr int refuse() noexcept {
    s::ensure_crash_projectable<NoCrashBranch, Bob, s::NoReliableRoles>();
    return 0;
}

}  // namespace

int main() { return refuse(); }
