// Sanitized names a check that ran: the sanitizer accepted the bytes.  The
// factory opened for every tag, so mint_tagged<Sanitized>(raw) built a
// value that claimed a sanitize pass nobody ran, with no retag call to
// name the edge.  An earned tag is reached only by retag along its
// discharge edge now, and the factory refuses it at its constraint.

#include <fixy/Tagged.h>

namespace tags = ::fixy::tags;

int main() {
    auto forged = fixy::mint_tagged<tags::source::Sanitized>(7);
    return forged.value();
}
