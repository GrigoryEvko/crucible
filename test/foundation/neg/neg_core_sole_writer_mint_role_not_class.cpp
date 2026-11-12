// The role of a sole writer is an empty class that names a kind of cell,
// such as the head of a ring.  An int names no kind of cell, so the mint
// refuses it.

#include <foundation/Linear.h>
#include <foundation/core/Atomic.h>

#include <utility>

int main() {
    auto writer = ::foundation::core::mint_sole_writer<int>();
    ::foundation::linear::drop(std::move(writer));
    return 0;
}
