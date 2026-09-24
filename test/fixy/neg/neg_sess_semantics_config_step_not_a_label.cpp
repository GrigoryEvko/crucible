// A step of a configuration takes a label of Definition 4.9.  An Actions
// list is no label, so the alias refuses it rather than answer
// NoTransition.

#include <fixy/session/Semantics.h>

namespace {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

using Next = s::config::step_t<s::TypingContext<>, g::Actions<>, s::EveryRoleReliable>;

}  // namespace

int main() { return 0; }
