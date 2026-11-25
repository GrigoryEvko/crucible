// Negative compile test.  This file must fail to compile.
//
// The bytes of a pool are for a read.  A kernel writes its outputs through
// with_output_views of the context, so an assignment of a byte of the View of
// bytes() is refused.

#include <crucible/PoolAllocator.h>

namespace {

[[maybe_unused]] void write_first_byte(crucible::PoolAllocator const& pool) {
    for (std::uint8_t& byte : pool.bytes())
        byte = 1;
}

}  // namespace

int main() { return 0; }
