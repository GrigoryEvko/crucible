// The compile-time checks of crucible/observe/HdrHistogram.h.

#include <crucible/observe/HdrHistogram.h>

namespace crucible::observe {

// A standard library may substitute mutex-backed operations where the target
// lacks the intrinsic, silently putting a lock inside every record. Refuse to
// build instead.
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "std::atomic<uint64_t> must be lock-free on this target.");

}  // namespace crucible::observe
