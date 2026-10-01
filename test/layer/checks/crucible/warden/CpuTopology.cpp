// The compile-time checks of crucible/warden/CpuTopology.h.

#include <crucible/warden/CpuTopology.h>

namespace crucible::warden {

// glibc has fixed this at 1024 ever since the fixed-size CPU set macros
// shipped. A host with more CPUs than that needs the dynamic-set calls,
// which nothing in CpuTopology.h uses. A smaller value on some other
// library would make the fallback of allowed_cpus truncate in silence, so
// it is caught here.
#ifdef __linux__
static_assert(CPU_SETSIZE >= 1024, "The allowed_cpus fallback iterates a fixed-size CPU set and assumes it "
                                   "holds at least 1024 CPUs.");
#endif

}  // namespace crucible::warden
