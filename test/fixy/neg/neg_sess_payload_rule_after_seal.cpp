// The payload rules of the session layer are sealed in
// fixy/session/Protocol.h.  A rule that a later header adds would change
// whether a payload can be sent, and which label a branch names, in the
// translation units that see it and not in the others.  So the next read
// of a payload rule counts the rules again, and a count that differs
// from the seal stops the build.

#include <fixy/session/Protocol.h>

namespace {

template <class T>
struct Opaque {};

}  // namespace

namespace fixy::session::combinators {
inline constexpr ::foundation::algebra::transition::payload_rule opaque{
    .shape = ^^::Opaque, .is_sendable = false, .is_label = false};
}  // namespace fixy::session::combinators

int main() {
    return ::fixy::session::is_well_formed_v<::fixy::session::Send<Opaque<int>, ::fixy::session::End>> ? 0 : 1;
}
