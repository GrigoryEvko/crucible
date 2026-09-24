// A ring of six slots.  The cursor reduces its count to a slot with a mask,
// which is the remainder only for a power of two, so the capacity must be
// one.

#include <fixy/CyclicBuffer.h>

int main() {
    ::fixy::CyclicBuffer<int, 6> ring{};
    return static_cast<int>(sizeof ring);
}
