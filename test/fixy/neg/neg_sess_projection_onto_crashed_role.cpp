// Alice crashed.  A crashed role has the type stop, not a projection.

#include <fixy/session/Projection.h>

namespace {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

struct Alice {};
struct Bob {};
struct Carol {};
struct Ping {};
struct Pong {};

using WithCrashBranch = g::Comm<Alice, Bob, g::Branch<Ping, int, g::End>, g::Branch<g::CrashLabel, void, g::End>>;
using AliceCrashed = g::remove_role_t<WithCrashBranch, Alice>;

constexpr int refuse() noexcept {
    s::ensure_crash_projectable<AliceCrashed, Alice, s::NoReliableRoles>();
    return 0;
}

}  // namespace

int main() { return refuse(); }
