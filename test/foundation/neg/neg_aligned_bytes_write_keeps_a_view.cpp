// The writer of an UnwrittenBytes gets a WriteLease that is a local of the
// door.  A writer that captures an Option of a View by reference could keep a
// borrow of the lease there, and the caller could read it after the block came
// back as an AlignedBytes.  The door refuses such a writer
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h).

#include <foundation/AlignedBytes.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <utility>

int main() {
    ::foundation::UnwrittenBytes block = ::foundation::mint_aligned_bytes(
        ::foundation::effects::testing::test().alloc, ::foundation::alloc::Layout::of<unsigned long>());
    ::foundation::core::Option<::foundation::core::View<std::uint8_t const>> kept{::foundation::core::none};
    ::foundation::AlignedBytes const bytes =
        std::move(block).write([&kept](::foundation::core::WriteLease<std::uint8_t>& lease) noexcept {
            static_cast<void>(kept.is_some());
            while (lease.remaining() != 0)
                lease.put(std::uint8_t{1});
        });
    return static_cast<int>(bytes.size());
}
