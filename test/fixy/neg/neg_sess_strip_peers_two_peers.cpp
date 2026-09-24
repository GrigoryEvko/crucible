// The binary view of a local type drops the peer of each message.  The
// ring projection of Alice sends to Bob and receives from Carol, so one
// binary channel would carry the traffic of two peers, and a label would
// reach the wrong process.  strip_peers_t requires one peer.

#include <fixy/session/Projection.h>

namespace {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

struct Alice {};
struct Bob {};
struct Carol {};
struct Token {};

using Ring = g::Rec<g::Msg<Alice, Bob, Token, int, g::Msg<Bob, Carol, Token, int, g::Msg<Carol, Alice, Token, int, g::Var>>>>;

using AliceLocal = typename s::project_t<Ring, Alice>::local;

using Refused = s::strip_peers_t<AliceLocal>;

}  // namespace

int main() { return 0; }
