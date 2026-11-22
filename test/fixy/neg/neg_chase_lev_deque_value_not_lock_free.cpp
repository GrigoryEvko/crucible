// A cell holds its value in an Atomic of the base, and a thief reads it
// without a lock.  A 64-byte value fits no word of a lock-free atomic
// instruction, so DequeValue refuses it, and the caller passes an index into
// storage of its own instead.

#include <fixy/concurrent/ChaseLevDeque.h>

#include <array>
#include <cstdint>

namespace {

struct WideValue {
    std::array<std::uint64_t, 8> words;
};

}  // namespace

template class ::fixy::concurrent::ChaseLevDeque<WideValue, 8>;

int main() { return 0; }
