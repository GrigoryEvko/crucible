// The set of reliable roles is a ReliableSet.  A plain role list, or any
// other type, is refused before the projection reads it.

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

constexpr int refuse() noexcept {
    s::ensure_crash_live_by_construction<WithCrashBranch, g::Roles<Alice>>();
    return 0;
}

}  // namespace

int main() { return refuse(); }
