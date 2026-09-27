// The sum of three rows whose first two wrap on one axis names no type.
// The refusal is the constraint of concurrent_row_n_t, before the fold
// names a partial sum that wraps.

#include <foundation/effects/Concurrent.h>

#include <cstdint>

namespace fe = ::foundation::effects;
namespace fr = ::foundation::effects::resource;

static_assert(requires {
    typename fe::concurrent_row_n_t<fe::ConcurrentRow<fr::SmBudget<UINT64_MAX>>, fe::ConcurrentRow<fr::SmBudget<1>>,
                                    fe::ConcurrentRow<>>;
});

int main() { return 0; }
