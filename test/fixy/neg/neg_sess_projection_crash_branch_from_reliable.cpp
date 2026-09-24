// A crash branch for a sender in the reliable set.  The branch can never
// run, and rule Sub-& lets no implementation omit it.

#include <fixy/session/Projection.h>

namespace {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

struct Alice {};
struct Bob {};
struct Carol {};
struct Ping {};
struct Pong {};

using DeadBranch = g::Comm<Alice, Bob, g::Branch<Ping, int, g::End>, g::Branch<g::CrashLabel, void, g::End>>;

constexpr int refuse() noexcept {
    s::ensure_crash_projectable<DeadBranch, Bob, s::ReliableSet<Alice>>();
    return 0;
}

}  // namespace

int main() { return refuse(); }
