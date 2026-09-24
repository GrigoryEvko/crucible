// A buffer of doubles at an alignment of 4.  Every element past the first
// could sit off the alignment of a double.

#include <foundation/AlignedBuffer.h>

int main() {
    auto buffer = ::foundation::AlignedBuffer<double, 4>::allocate(4);
    return buffer.size() == 4 ? 0 : 1;
}
