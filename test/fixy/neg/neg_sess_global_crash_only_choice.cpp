// A transmission whose only branch is the crash branch.  No role sends
// the crash label, so Bob could only wait for Alice to crash.

#include <fixy/session/Projection.h>

namespace {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

struct Alice {};
struct Bob {};
struct Carol {};
struct Ping {};
struct Pong {};

using OnlyCrash = g::Msg<Alice, Bob, g::CrashLabel, void, g::End>;

constexpr int refuse() noexcept {
    g::ensure_global_well_formed<OnlyCrash>();
    return 0;
}

}  // namespace

int main() { return refuse(); }
