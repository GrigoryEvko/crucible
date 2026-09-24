// A copy of an aligned buffer.  Two buffers would own one allocation, and
// the second destructor would free it again.  The copy constructor is
// deleted.

#include <foundation/AlignedBuffer.h>

int main() {
    auto buffer = ::foundation::AlignedBuffer<int>::allocate(4);
    auto second = buffer;
    return second.size() == 4 ? 0 : 1;
}
