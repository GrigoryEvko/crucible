// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Pool stores jobs in an inline, fixed-size task cell.  A closure
// larger than that budget must be rejected at compile time instead of
// silently falling back to heap allocation through type erasure.

#include <crucible/concurrent/AdaptiveScheduler.h>

#include <array>
#include <cstdint>

int main() {
    namespace cc = crucible::concurrent;
    namespace cs = crucible::concurrent::scheduler;

    cc::Pool<cs::Fifo> pool{cc::CoreCount{1}};
    std::array<std::uint64_t,
               cc::adaptive_detail::InlineTask<>::capacity()> payload{};
    cc::dispatch(pool, [payload] noexcept {
        (void)payload[0];
    });
    return 0;
}
