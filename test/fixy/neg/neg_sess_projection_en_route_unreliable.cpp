// Bob is the receiver of an en-route message from Alice, who is not
// reliable.  Bob's type is the whole choice with its crash branch, and
// this en-route node has one branch and no crash branch.

#include <fixy/session/Projection.h>

namespace {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

struct Alice {};
struct Bob {};
struct Carol {};
struct Ping {};
struct Pong {};

using Sent = g::EnRoute<Alice, Bob, Ping, int, g::End>;

constexpr int refuse() noexcept {
    s::ensure_crash_projectable<Sent, Bob, s::NoReliableRoles>();
    return 0;
}

}  // namespace

int main() { return refuse(); }
