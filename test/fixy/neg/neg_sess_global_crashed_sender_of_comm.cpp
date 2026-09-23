// A crashed role as the sender of a transmission.  A crashed role sends
// nothing: role removal turns its transmission into the crash
// pseudo-message, an EnRoute node.

#include <fixy/session/Projection.h>

namespace {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

struct Alice {};
struct Bob {};
struct Carol {};
struct Ping {};
struct Pong {};

using CrashedSender = g::Msg<g::Crashed<Alice>, Bob, Ping, int, g::End>;

constexpr int refuse() noexcept {
    g::ensure_global_well_formed<CrashedSender>();
    return 0;
}

}  // namespace

int main() { return refuse(); }
