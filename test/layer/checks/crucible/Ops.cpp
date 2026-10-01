// The compile-time checks of crucible/Ops.h.

#include <crucible/Ops.h>

namespace crucible {

// Lookup tables sized by the sentinel, and every place that round-trips an
// operation through a single byte, depend on this. Growing past it means
// widening the underlying type and auditing each of those places.
static_assert(static_cast<unsigned>(Op::NUM_OPS) <= 256, "the underlying type is uint8_t, so NUM_OPS must fit in it");

}  // namespace crucible
