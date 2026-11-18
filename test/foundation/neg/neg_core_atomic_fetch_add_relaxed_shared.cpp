// Only a count adds with relaxed order.  A cell that each thread can write
// can publish data, so each of its additions takes the acq_rel order.

#include <foundation/core/Atomic.h>

#include <cstdint>

int main() {
    ::foundation::core::Atomic<std::uint64_t> cell{0};
    return static_cast<int>(cell.fetch_add_relaxed(1));
}
