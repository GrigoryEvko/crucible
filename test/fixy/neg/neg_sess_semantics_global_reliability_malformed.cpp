// The reliability assumption of the global semantics is a ReliableSet or
// EveryRoleReliable.  A bare role list is neither, and a reading of it as
// "no role reliable" would let every role crash, so the alias refuses it.

#include <fixy/session/Semantics.h>

namespace {

struct P {};

namespace g = ::fixy::session::global;

using Labels = g::state_enabled_t<g::State<g::Roles<>, g::End>, g::Roles<P>>;

}  // namespace

int main() { return 0; }
