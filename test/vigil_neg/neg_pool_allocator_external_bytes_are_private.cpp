// Negative-compile test.  This file must fail to compile.
//
// A pool takes the bytes of an external slot only from a scope of its context,
// which drops them.  Bytes that a caller registers would stay in the pool after
// the end of their owner.

#include <crucible/PoolAllocator.h>

int main() {
    crucible::PoolAllocator pool;
    pool.register_external_bytes_(crucible::SlotId{0}, ::fixy::View<std::uint8_t>{});
    return 0;
}
