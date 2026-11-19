// The labels that one role offers come from role_actions_of of
// fixy/session/Semantics.h.  The relation was once the class template
// role_actions.  This file tries to hide a receive: it specializes that
// class template for a role at a Recv, so that the role offers no label.
// The relation is a function, and no class template gives it, so the
// specialization does not compile.

#include <fixy/session/Semantics.h>
#include <foundation/Quarantine.h>

namespace probe {
struct Alice {};
struct Bob {};
struct Hello {};
using Empty = fixy::session::TypingContext<>;
using Receives = fixy::session::Recv<fixy::session::PeerMsg<Alice, Hello, int>, fixy::session::End>;
}  // namespace probe

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture specializes a trait of the base, and the compiler refuses it")
template <>
struct fixy::session::config::detail::role_actions<probe::Empty, probe::Bob, probe::Receives> {
    using type = fixy::session::global::Actions<>;
};
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
