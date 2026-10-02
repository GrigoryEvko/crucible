// A compare-and-swap compares the bits of the value.  The padding of a
// class holds bits that no member sets, so two equal values can compare
// unequal, and the value gate refuses a class with padding.

#include <foundation/core/Atomic.h>

#include <cstdint>

struct Padded {
    std::uint32_t key = 0;
    std::uint16_t tag = 0;
};

int main() { return static_cast<int>(::foundation::core::Atomic<Padded>{}.load_acquire().key); }
