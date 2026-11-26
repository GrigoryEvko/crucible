// The compile-time checks of fixy/concurrent/LaneRing.h.

#include <fixy/concurrent/LaneRing.h>

#include <foundation/core/CaptureStore.h>

#include <cstdint>

namespace fixy::concurrent {

namespace detail::lane_ring_checks {

struct LanesOwner final {};
using Ring = LaneRing<std::uint64_t, 4, LaneCount{2}, LanesOwner>;

// The ring states synchronized_by_proof{}, and the check of the mark holds on
// it: no door keeps a borrow that a caller gives it.
static_assert(::foundation::core::detail::synchronized_by_proof_check_v<Ring>);

}  // namespace detail::lane_ring_checks

}  // namespace fixy::concurrent
