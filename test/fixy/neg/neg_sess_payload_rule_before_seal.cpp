// Negative compile test.  This file must fail to compile.
//
// A payload rule that this file writes into the family `core` before
// fixy/session/Protocol.h opens that family stands in the family when a
// query first reads it.  The count then differs from the seal of the
// family, and the query stops the build before it can read the rule.
//
// Expected diagnostic: a registration stands outside its seal.

#include <fixy/session/Registry.h>
#include <foundation/Quarantine.h>

namespace {
template <class T>
struct Opaque {};
}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
namespace fixy::session::combinators::core {
inline constexpr ::fixy::session::transition::payload_rule opaque{
    .shape = ^^::Opaque, .is_sendable = false, .is_label = false};
}  // namespace fixy::session::combinators::core
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

#include <fixy/session/Protocol.h>

int main() {
    return ::fixy::session::is_well_formed_v<::fixy::session::Send<Opaque<int>, ::fixy::session::End>> ? 0 : 1;
}
