// Two rows whose sum on the HBM axis passes the top of uint64_t have no
// sum type.  The alias requires ConcurrentlySchedulable, so the wrapped
// value is never spelled as a tag.

#include <foundation/effects/Concurrent.h>

#include <cstdint>

namespace fe = ::foundation::effects;

static_assert(requires {
    typename fe::concurrent_row_sum_t<fe::ConcurrentRow<fe::resource::HbmBytes<UINT64_MAX>>,
                                      fe::ConcurrentRow<fe::resource::HbmBytes<2>>>;
});

int main() { return 0; }
