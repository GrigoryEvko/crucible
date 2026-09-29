// Sanitized names a check that ran: the sanitizer accepted the bytes.  A
// factory open for every tag would let mint_tagged<Sanitized>(raw) build
// a value that claims a sanitize pass nobody ran, with no retag call to
// name the edge.  An earned tag is reached only by retag along its
// discharge edge, and the factory refuses it at its constraint.

#include <fixy/Tagged.h>

namespace tags = ::fixy::tags;

int main() {
    auto forged = fixy::mint_tagged<tags::source::Sanitized>(7);
    return forged.value();
}
