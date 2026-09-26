// The payload order of the session layer weakens a refinement along the
// implication relation only where the value type evaluates both
// predicates.  A trusted floor of 256 over a std::uint8_t holds a value
// that no check saw, so it does not weaken to positive.

#include <fixy/session/Subtype.h>

#include <cstdint>

namespace {

template <class Sub, class Super>
    requires fixy::session::is_payload_subsort_v<Sub, Super>
constexpr void accept_subsort() noexcept {}

}  // namespace

int main() {
    accept_subsort<fixy::Refined<fixy::bounded_below<256>, std::uint8_t>, fixy::Positive<std::uint8_t>>();
    return 0;
}
