// Negative compile test.  This file must fail to compile.
//
// A temporary pool releases its columns at the end of the statement, so it
// lends no column.

#include <crucible/PoolAllocator.h>

namespace {

[[maybe_unused]] std::size_t read_temporary(crucible::PoolAllocator const&& pool,
                                            crucible::PoolAllocator::InitializedView const& live) {
    return static_cast<crucible::PoolAllocator const&&>(pool).with_addresses(
        live, [](crucible::AddressColumn column) noexcept { return column.size(); });
}

}  // namespace

int main() { return 0; }
