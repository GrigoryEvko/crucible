// The compile-time checks of fixy/handle/OneShotFlag.h.

#include <fixy/handle/OneShotFlag.h>

namespace fixy::handle {

static_assert(alignof(OneShotFlag) >= 64, "OneShotFlag must be cache-line aligned to prevent false "
                                          "sharing on the cross-thread signal path.");
static_assert(sizeof(OneShotFlag) >= 64, "OneShotFlag occupies a full cache line by construction; "
                                         "embedders rely on the flag NOT sharing a line with any "
                                         "field touched on the consumer's hot path.");

}  // namespace fixy::handle
