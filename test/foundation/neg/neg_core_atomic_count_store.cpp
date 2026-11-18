// A count has no store.  Its changes take the relaxed order, so a store
// through it would publish no data, and the cell refuses each store.

#include <foundation/core/Atomic.h>

#include <cstdint>

int main() {
    ::foundation::core::Atomic<std::uint64_t, ::foundation::core::Counter> count{};
    count.store_release(5);
    return static_cast<int>(count.load_acquire());
}
