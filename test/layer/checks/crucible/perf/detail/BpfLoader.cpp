// The compile-time checks of crucible/perf/detail/BpfLoader.h.

#include <crucible/perf/detail/BpfLoader.h>

namespace crucible::perf::detail {

static_assert(sizeof(Tgid) == sizeof(uint32_t), "Tagged<uint32_t, source::Kernel> must be the same width as "
                                                "uint32_t.  The empty trust-lattice element must collapse "
                                                "under EBO.");
static_assert(sizeof(Tid) == sizeof(uint32_t));
static_assert(sizeof(Fd) == sizeof(int));

}  // namespace crucible::perf::detail
