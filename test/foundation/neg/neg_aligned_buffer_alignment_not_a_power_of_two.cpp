// A buffer at an alignment of 24.  aligned_alloc takes a power of two only,
// and the byte count rounds by masking with the alignment less one, which
// is wrong for any other value.

#include <foundation/AlignedBuffer.h>

int main() {
    auto buffer = ::foundation::AlignedBuffer<int, 24>::allocate(4);
    return buffer.size() == 4 ? 0 : 1;
}
