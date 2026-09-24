// A step of the global semantics takes a label of Definition 4.9.  An int
// is no label, so the alias refuses it rather than answer NoTransition.

#include <fixy/session/Semantics.h>

namespace {

namespace g = ::fixy::session::global;

using Next = g::state_step_t<g::State<g::Roles<>, g::End>, int, ::fixy::session::EveryRoleReliable>;

}  // namespace

int main() { return 0; }
