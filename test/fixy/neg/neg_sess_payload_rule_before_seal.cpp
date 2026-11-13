// A payload rule declared before fixy/session/Protocol.h stands in the
// registry when the seal of that header is counted.  The count at the
// foot of the header then differs from the seal, and the build stops
// there, before a query can read the rule.

#include <fixy/session/Registry.h>
#include <foundation/Quarantine.h>

namespace {

template <class T>
struct Opaque {};

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
namespace fixy::session::combinators {
inline constexpr ::fixy::session::transition::payload_rule opaque{
    .shape = ^^::Opaque, .is_sendable = false, .is_label = false};
}  // namespace fixy::session::combinators
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

#include <fixy/session/Protocol.h>

int main() { return 0; }
