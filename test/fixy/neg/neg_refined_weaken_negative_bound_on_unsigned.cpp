// The payload order of the session layer weakens a refinement along the
// implication relation only where the value type evaluates both
// predicates.  A trusted ceiling of -1 over an unsigned value does not
// weaken to a ceiling of 0.

#include <fixy/session/Subtype.h>

namespace {

template <class Sub, class Super>
    requires fixy::session::is_payload_subsort_v<Sub, Super>
constexpr void accept_subsort() noexcept {}

}  // namespace

int main() {
    accept_subsort<fixy::Refined<fixy::bounded_above<-1>, unsigned>, fixy::Refined<fixy::bounded_above<0>, unsigned>>();
    return 0;
}
