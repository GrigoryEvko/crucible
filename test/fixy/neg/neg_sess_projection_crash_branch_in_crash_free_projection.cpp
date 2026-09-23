// project_t counts every role as reliable.  A protocol with a crash
// branch goes through project_crash_t with its reliable set, not
// through the crash-free projection.

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
    s::ensure_projectable<WithCrashBranch, Bob>();
    return 0;
}

}  // namespace

int main() { return refuse(); }
