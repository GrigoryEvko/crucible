// A cell holds its value in std::atomic<T>, and a thief reads it without a
// lock.  A 64-byte value is not always lock-free, so DequeValue refuses it
// and the caller passes a pointer instead.

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
