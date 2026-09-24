// project_t requires a well-formed global type.  A Var with no
// enclosing Rec is a loop-back to nothing, and its projection would be a
// Continue with no Loop.  The requirement refuses it before any
// projection rule runs.

#include <fixy/session/Projection.h>

namespace {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

struct Alice {};
struct Bob {};
struct Tick {};

using Dangling = g::Msg<Alice, Bob, Tick, int, g::Var>;

using Refused = s::project_t<Dangling, Alice>;

}  // namespace

int main() { return 0; }
