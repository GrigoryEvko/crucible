// A borrow of a read-only handle asks for the write right.  A borrow gives
// only the rights that its handle holds, so restrict refuses a right that the
// handle lacks, and a write door never gets the descriptor.

#include <fixy/os/Handle.h>

namespace os = fixy::os;

int main() {
    os::Handle<os::kind::File, os::RightSet::read> const file{};
    [[maybe_unused]] auto writer = file.ref().restrict<os::RightSet::write>();
    return 0;
}
