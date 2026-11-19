// The receipt of a message ([Γ-&]) comes from receipt_of of
// fixy/session/Semantics.h.  The rule was once the class template receipt.
// This file tries to give a role a receipt that no queue holds: it
// specializes that class template for an empty queue, so that the role
// receives a message.  The rule is a function, and no class template gives
// it, so the specialization does not compile.

#include <fixy/session/Semantics.h>
#include <foundation/Quarantine.h>

namespace probe {
struct Alice {};
struct Bob {};
struct Hello {};
using Receives = fixy::session::Recv<fixy::session::PeerMsg<Alice, Hello, int>, fixy::session::End>;
using Received = fixy::session::global::Actions<fixy::session::global::RecvAction<Bob, Alice, Hello, int>>;
}  // namespace probe

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture specializes a rule of the semantics, and the compiler refuses it")
template <>
struct fixy::session::config::detail::receipt<probe::Bob, probe::Alice, void, probe::Receives> {
    using type = probe::Received;
};
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
