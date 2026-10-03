// The reliability assumption of a configuration is a ReliableSet or
// EveryRoleReliable.  A bare role is neither, so the alias refuses it.

#include <fixy/session/Semantics.h>

namespace neg_sess_semantics_config_reliability_malformed_types {

struct P {};

namespace s = ::fixy::session;

// The requirement is the one use of the labels, so the refusal is the only
// error.
static_assert(requires { typename s::config::enabled_t<s::TypingContext<>, P>; });

}  // namespace neg_sess_semantics_config_reliability_malformed_types

int main() { return 0; }
