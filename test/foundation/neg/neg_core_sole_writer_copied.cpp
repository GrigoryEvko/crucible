// A sole writer is linear.  A copy would give the cell a second writer, so
// the copy constructor is deleted.

#include <foundation/Linear.h>
#include <foundation/core/Atomic.h>

#include <utility>

namespace {
struct HeadRole {};
}  // namespace

int main() {
    auto writer = ::foundation::core::mint_sole_writer<HeadRole>();
    auto copy = writer;
    ::foundation::linear::drop(std::move(copy));
    ::foundation::linear::drop(std::move(writer));
    return 0;
}
