// A step of a configuration takes a label of Definition 4.9.  An Actions
// list is no label, so the alias refuses it rather than answer
// NoTransition.

#include <fixy/session/Semantics.h>

namespace neg_sess_semantics_config_step_not_a_label_types {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

using Next = s::config::step_t<s::TypingContext<>, g::Actions<>, s::EveryRoleReliable>;

}  // namespace neg_sess_semantics_config_step_not_a_label_types

int main() { return 0; }
