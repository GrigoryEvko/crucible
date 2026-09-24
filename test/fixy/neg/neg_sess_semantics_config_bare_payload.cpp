// A configuration names the peer of each message with a PeerMsg.  A Recv
// of a bare payload does not say whose queue to read, so no rule reads
// the entry, and the alias refuses the configuration.

#include <fixy/session/Semantics.h>

namespace neg_sess_semantics_config_bare_payload_types {

struct P {};

namespace s = ::fixy::session;

using Bare = s::TypingContext<s::RoleState<P, s::OutQueue<>, s::Recv<int, s::End>>>;
using Labels = s::config::enabled_t<Bare, s::EveryRoleReliable>;

}  // namespace neg_sess_semantics_config_bare_payload_types

int main() { return 0; }
