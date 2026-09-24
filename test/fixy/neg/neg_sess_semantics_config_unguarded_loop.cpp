// A Loop body of a configuration starts with an action.  The body of
// Loop<Continue> is its own loop-back, so its unfolding never ends, and
// the alias refuses the configuration before any walk unfolds it.

#include <fixy/session/Semantics.h>

namespace {

struct P {};
struct Q {};
struct M {};

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

using Spins = s::TypingContext<s::RoleState<P, s::OutQueue<>, s::Loop<s::Continue>>>;
using Next = s::config::step_t<Spins, g::SendAction<P, Q, M, int>, s::EveryRoleReliable>;

}  // namespace

int main() { return 0; }
