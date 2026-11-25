// Negative compile test.  This file must fail to compile.
//
// The bytes of a pool are for a read, so a fill through the View of bytes()
// is refused.

#include <crucible/PoolAllocator.h>

namespace {

[[maybe_unused]] void fill_pool(crucible::PoolAllocator const& pool) { ::fixy::fill(pool.bytes(), std::uint8_t{1}); }

}  // namespace

int main() { return 0; }
