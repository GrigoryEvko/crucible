// safe_add_all checks each partial sum, not only the last one.  Here the
// first two terms already wrap std::uint32_t, and the third term is small.

#include <fixy/Checked.h>

#include <cstdint>
#include <limits>

inline constexpr std::uint32_t kHalf = std::numeric_limits<std::uint32_t>::max() / 2 + 1;
inline constexpr std::uint32_t kTotal = fixy::safe_add_all<std::uint32_t, kHalf, kHalf, 100u>;

int main() { return static_cast<int>(kTotal & 1u); }
