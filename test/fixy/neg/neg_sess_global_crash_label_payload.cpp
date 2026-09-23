// A crash branch with a payload.  The crash label is a pseudo-message,
// so no value can arrive with it.

#include <fixy/session/Projection.h>

namespace {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

struct Alice {};
struct Bob {};
struct Carol {};
struct Ping {};
struct Pong {};

using CrashWithPayload = g::Comm<Alice, Bob, g::Branch<Ping, int, g::End>, g::Branch<g::CrashLabel, int, g::End>>;

constexpr int refuse() noexcept {
    g::ensure_global_well_formed<CrashWithPayload>();
    return 0;
}

}  // namespace

int main() { return refuse(); }
