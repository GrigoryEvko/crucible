// project_t requires a global type.  A local type from Protocol.h is not
// one: it has no roles to project onto.  The requirement refuses it
// before any projection rule runs.

#include <fixy/session/Projection.h>

namespace {

namespace s = ::fixy::session;

struct Alice {};

using Refused = s::project_t<s::Send<int, s::End>, Alice>;

}  // namespace

int main() { return 0; }
