// The compile-time checks of crucible/Philox.h.

#include <crucible/Philox.h>

namespace crucible {

// The hexadecimal 2^-32 has the same bits as the decimal literal the
// conversions used before, so no stream changes.
static_assert(std::bit_cast<std::uint32_t>(Philox::to_uniform(1u))
              == std::bit_cast<std::uint32_t>(2.3283064365386963e-10f));
static_assert(std::bit_cast<std::uint64_t>(Philox::to_uniform_d(1u))
              == std::bit_cast<std::uint64_t>(2.3283064365386963e-10));

}  // namespace crucible
