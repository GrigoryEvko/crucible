// Negative compile test.  This file must fail to compile.
//
// with_addresses lends the address column of a pool for the time of its call.
// A body that gives the View of the column back leaves a borrow of a column
// that the pool can end.

#include <crucible/PoolAllocator.h>

namespace {

[[maybe_unused]] auto keep_column(crucible::PoolAllocator const& pool,
                                  crucible::PoolAllocator::InitializedView const& live) {
    return pool.with_addresses(live, [](crucible::AddressColumn column) noexcept { return column; });
}

}  // namespace

int main() { return 0; }
