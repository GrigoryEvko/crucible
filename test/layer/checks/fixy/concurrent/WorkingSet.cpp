// The compile-time checks of fixy/concurrent/WorkingSet.h.

#include <fixy/concurrent/WorkingSet.h>

namespace fixy::concurrent {

static_assert(conservative_l1d_per_core < conservative_l2_per_core);
static_assert(conservative_l2_per_core < conservative_l3_total);

}  // namespace fixy::concurrent
