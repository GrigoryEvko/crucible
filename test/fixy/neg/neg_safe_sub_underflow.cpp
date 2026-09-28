// safe_sub is a difference that the compiler does.  An unsigned difference
// below zero stops the build, and it does not become a size near the top of
// std::size_t.

#include <fixy/Checked.h>

#include <cstddef>

inline constexpr std::size_t kDifference = fixy::safe_sub<std::size_t, 10u, 100u>;

int main() { return static_cast<int>(kDifference & 1u); }
