// A Loop body of a configuration starts with an action.  The body of
// Loop<Continue> is its own loop-back, so its unfolding never ends, and
// the alias refuses the configuration before any walk unfolds it.

#include <fixy/session/Semantics.h>

namespace neg_sess_semantics_config_unguarded_loop_types {

struct P {};
struct Q {};
struct M {};

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

using Spins = s::TypingContext<s::RoleState<P, s::OutQueue<>, s::Loop<s::Continue>>>;
// The requirement is the one use of the step, so the refusal is the only
// error.
static_assert(requires { typename s::config::step_t<Spins, g::SendAction<P, Q, M, int>, s::EveryRoleReliable>; });

}  // namespace neg_sess_semantics_config_unguarded_loop_types

int main() { return 0; }
