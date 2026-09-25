// Two rows whose sum on one axis passes the top of uint64_t would read
// as a small demand after the wrap.  A schedule that constrains on
// ConcurrentlySchedulable refuses the pair.

#include <foundation/effects/Concurrent.h>

#include <cstdint>

namespace fe = ::foundation::effects;

template <class First, class Second>
    requires fe::ConcurrentlySchedulable<First, Second>
void schedule_together() noexcept {}

int main() {
    schedule_together<fe::ConcurrentRow<fe::resource::HbmBytes<UINT64_MAX>>,
                      fe::ConcurrentRow<fe::resource::HbmBytes<1>>>();
    return 0;
}
