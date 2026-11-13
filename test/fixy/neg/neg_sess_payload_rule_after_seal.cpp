// The payload rules of the session layer are sealed in
// fixy/session/Protocol.h.  A rule that a later header adds would change
// whether a payload can be sent, and which label a branch names, in the
// translation units that see it and not in the others.  So the next read
// of a payload rule counts the rules again, and a count that differs
// from the seal stops the build.

#include <fixy/session/Protocol.h>
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

int main() {
    return ::fixy::session::is_well_formed_v<::fixy::session::Send<Opaque<int>, ::fixy::session::End>> ? 0 : 1;
}
