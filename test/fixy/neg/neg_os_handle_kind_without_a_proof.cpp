// The table of the kinds states the type of the file of each kind (the kinds
// of fixy/os/Handle.h).  A kind with no row states none, so a Handle of it
// does not compile.

#include <fixy/os/Handle.h>

namespace os = fixy::os;

namespace {
struct BlockDevice final {};
}  // namespace

int main() {
    [[maybe_unused]] os::Handle<BlockDevice, os::RightSet::read> const device{};
    return 0;
}
