// A step of the global semantics takes a label of Definition 4.9.  An int
// is no label, so the alias refuses it rather than answer NoTransition.

#include <fixy/session/Semantics.h>

namespace neg_sess_semantics_global_step_not_a_label_types {

namespace g = ::fixy::session::global;

// The requirement is the one use of the step, so the refusal is the only
// error.
static_assert(requires {
    typename g::state_step_t<g::State<g::Roles<>, g::End>, int, ::fixy::session::EveryRoleReliable>;
});

}  // namespace neg_sess_semantics_global_step_not_a_label_types

int main() { return 0; }
