// A payload rule declared before fixy/session/Protocol.h stands in the
// registry when the seal of that header is counted.  The count at the
// foot of the header then differs from the seal, and the build stops
// there, before a query can read the rule.

#include <foundation/algebra/Transition.h>

namespace {

template <class T>
struct Opaque {};

}  // namespace

namespace fixy::session::combinators {
inline constexpr ::foundation::algebra::transition::payload_rule opaque{
    .shape = ^^::Opaque, .is_sendable = false, .is_label = false};
}  // namespace fixy::session::combinators

#include <fixy/session/Protocol.h>

int main() { return 0; }
