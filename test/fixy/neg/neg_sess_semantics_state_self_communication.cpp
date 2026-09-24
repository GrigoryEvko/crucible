// A state of the global semantics holds a well-formed global type.  A
// role that sends to itself is not well-formed, so no rule reads the
// state, and the alias refuses it.

#include <fixy/session/Semantics.h>

namespace {

struct P {};
struct M {};

namespace g = ::fixy::session::global;

using SelfSend = g::State<g::Roles<>, g::Msg<P, P, M, int, g::End>>;
using Labels = g::state_enabled_t<SelfSend, ::fixy::session::EveryRoleReliable>;

}  // namespace

int main() { return 0; }
