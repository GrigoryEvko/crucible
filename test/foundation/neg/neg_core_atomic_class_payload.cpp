// An Atomic holds an integral or enum value.  A class can carry padding
// that a compare-and-swap would compare, so the value gate refuses it.

#include <foundation/core/Atomic.h>

struct Wrapped {
    int value = 0;
};

int main() { return ::foundation::core::Atomic<Wrapped>{}.load_acquire().value; }
