// The compile-time checks of crucible/Arena.h.

#include <crucible/Arena.h>

namespace crucible {

static_assert(sizeof(Arena) == 64, "Arena must fit within one cache line");

}  // namespace crucible
