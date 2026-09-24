// A copy of a table buffer.  Two buffers would own one allocation, and the
// second destructor would free it again.  The copy constructor is deleted.

#include <foundation/SwissTableBuffer.h>

int main() {
    auto table = ::foundation::SwissTableBuffer<const int*>::allocate(16);
    auto second = table;
    return second.capacity() == 16 ? 0 : 1;
}
