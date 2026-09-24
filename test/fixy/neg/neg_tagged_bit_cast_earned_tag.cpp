// Refusing mint_tagged for an earned tag closes the named door.  A
// trivially copyable wrapper left a second one: std::bit_cast built a
// Tagged<int, Sanitized> from any int with no constructor and no retag.
// Under an earned tag the assignments are user-provided now, so the class
// is not trivially copyable and bit_cast refuses it at its constraint.

#include <fixy/Tagged.h>

#include <bit>

namespace tags = ::fixy::tags;

int main() {
    auto forged = std::bit_cast<fixy::Tagged<int, tags::source::Sanitized>>(7);
    return forged.value();
}
