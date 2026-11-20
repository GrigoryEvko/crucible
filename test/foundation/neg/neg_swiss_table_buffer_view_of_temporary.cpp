// A View of the slots of a temporary table buffer.  The buffer frees its
// allocation at the end of the full expression, and the View would then
// point into freed storage.  The accessor of an rvalue buffer is deleted.

#include <foundation/SwissTableBuffer.h>

int main() {
    auto const slots = ::foundation::SwissTableBuffer<const int*>::allocate(16).slots();
    return slots.size() == 16 ? 0 : 1;
}
