// safe_capacity is the product of a count and an element size, and
// safe_mul does the multiplication.  A product that std::size_t cannot hold
// stops the build, and it does not size a buffer too small.

#include <fixy/Checked.h>

#include <cstddef>
#include <limits>

inline constexpr std::size_t kCapacity = fixy::safe_capacity<std::numeric_limits<std::size_t>::max(), 2u>;

int main() { return static_cast<int>(kCapacity & 1u); }
