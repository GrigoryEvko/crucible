// safe_add is a sum that the compiler does.  A sum that the destination
// type cannot hold stops the build, and it does not become a wrapped size.

#include <fixy/Checked.h>

#include <cstdint>

inline constexpr std::uint32_t kSum = fixy::safe_add<std::uint32_t, 0xFFFFFFFFu, 1u>;

int main() { return static_cast<int>(kSum & 1u); }
