// The crash label en route from a live role.  Only a crashed sender has
// the crash pseudo-message en route.

#include <fixy/session/Projection.h>

namespace {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

struct Alice {};
struct Bob {};
struct Carol {};
struct Ping {};
struct Pong {};

using ForgedDetection = g::EnRoute<Alice, Bob, g::CrashLabel, void, g::End>;

constexpr int refuse() noexcept {
    g::ensure_global_well_formed<ForgedDetection>();
    return 0;
}

}  // namespace

int main() { return refuse(); }
