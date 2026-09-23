// The liveness theorem of the crash-stop paper covers a global type
// written at design time.  A state after a crash is not one.

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
using BobCrashed = g::remove_role_t<WithCrashBranch, Bob>;

constexpr int refuse() noexcept {
    s::ensure_crash_live_by_construction<BobCrashed, s::NoReliableRoles>();
    return 0;
}

}  // namespace

int main() { return refuse(); }
