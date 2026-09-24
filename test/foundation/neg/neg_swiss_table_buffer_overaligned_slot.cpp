// A table whose slot type needs 32-byte alignment.  The slots start at the
// capacity offset, and a capacity is a multiple of the 16-byte group width
// only, so the first slot of a 16-slot table would sit off its alignment.
// The slot concept admits an alignment of 16 or less.

#include <foundation/SwissTableBuffer.h>

namespace {
struct alignas(32) WideSlot {
    unsigned long long lanes[4];
};
}  // namespace

int main() {
    auto table = ::foundation::SwissTableBuffer<WideSlot>::allocate(16);
    return table.capacity() == 16 ? 0 : 1;
}
