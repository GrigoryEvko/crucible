// The first transmission of the loop has a crash branch, and the second
// has none.  The gap is inside the loop body, one step after the choice.

#include <fixy/session/Projection.h>

namespace {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

struct Alice {};
struct Bob {};
struct Carol {};
struct Ping {};
struct Pong {};

using HiddenInLoop =
    g::Rec<g::Comm<Alice, Bob, g::Branch<Ping, int, g::Msg<Alice, Bob, Pong, int, g::Var>>,
                   g::Branch<g::CrashLabel, void, g::End>>>;

static_assert(g::is_balanced_plus_v<HiddenInLoop>);

constexpr int refuse() noexcept {
    s::ensure_crash_live_by_construction<HiddenInLoop, s::NoReliableRoles>();
    return 0;
}

}  // namespace

int main() { return refuse(); }
