// A borrow of a temporary handle.  The handle closes its descriptor at the end
// of the full expression, so the borrow would name a closed descriptor, and
// the kernel can give that number to a different file.

#include <fixy/os/Handle.h>

namespace os = fixy::os;

int main() {
    [[maybe_unused]] auto borrow = os::Handle<os::kind::File, os::RightSet::read>{}.ref();
    return 0;
}
