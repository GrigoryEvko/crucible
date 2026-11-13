// The compile-time checks of crucible/warden/CpuTopology.h.

#include <crucible/warden/CpuTopology.h>

namespace crucible::warden {

// A set is a whole number of words of 64 bits.
static_assert(CpuSet::capacity % 64 == 0);

// The fallback of allowed_cpus puts each CPU of the affinity mask of the
// kernel in a CpuSet.  A mask wider than the set would lose its high CPUs in
// silence, so it is caught here.
static_assert(::foundation::algebra::lattices::AffinityMask::kBits <= CpuSet::capacity,
              "The allowed_cpus fallback puts each CPU of an affinity mask in a CpuSet, so the set holds at least "
              "the width of the mask.");

}  // namespace crucible::warden
