// The shape message_socket of a socket tag counts only on a tag of the base.
// A tag outside the base that states it reads as a stream, because a wrong
// statement gives two readers of one stream.  So a duplicate of a socket of
// such a tag with the read right does not satisfy DuplicatesWith.

#include <fixy/os/Handle.h>

namespace os = fixy::os;

namespace {
struct[[= os::message_socket{}]] OutsideTag final {};
}  // namespace

static_assert(os::DuplicatesWith<os::kind::Socket<OutsideTag>, os::RightSet::read>,
              "a tag outside the base reads as a stream");

int main() { return 0; }
