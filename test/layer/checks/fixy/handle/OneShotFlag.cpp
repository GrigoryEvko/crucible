// The compile-time checks of fixy/handle/OneShotFlag.h.

#include <fixy/handle/OneShotFlag.h>

namespace fixy::handle {

static_assert(alignof(OneShotFlag) >= 64, "OneShotFlag must be cache-line aligned to prevent false "
                                          "sharing on the cross-thread signal path.");
static_assert(sizeof(OneShotFlag) >= 64, "OneShotFlag occupies a full cache line by construction; "
                                         "embedders rely on the flag NOT sharing a line with any "
                                         "field touched on the consumer's hot path.");

// The one member is an Atomic of the base, so two threads write a flag at the
// same time by its structure, and the class states no transfer fact.
static_assert(::foundation::transfer::Synchronized<OneShotFlag>);
static_assert(!::foundation::transfer::detail::states(^^OneShotFlag, ^^::foundation::transfer::synchronized));

}  // namespace fixy::handle
