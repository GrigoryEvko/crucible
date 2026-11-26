// The writer of mint_heap_array_written gets a WriteLease that is a local of
// the mint.  A writer that captures an Option of a View by reference could
// keep a borrow of the lease there, and the caller could read it after the
// mint gave the HeapArray.  The mint refuses such a writer
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h).

#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

int main() {
    ::foundation::core::Option<::foundation::core::View<std::uint64_t const>> kept{::foundation::core::none};
    auto const values = ::foundation::core::mint_heap_array_written<std::uint64_t>(
        ::foundation::effects::testing::test().alloc, std::size_t{4},
        [&kept](::foundation::core::WriteLease<std::uint64_t>& lease) noexcept {
            static_cast<void>(kept.is_some());
            while (lease.remaining() != 0)
                lease.put(std::uint64_t{1});
        });
    return static_cast<int>(values.size());
}
